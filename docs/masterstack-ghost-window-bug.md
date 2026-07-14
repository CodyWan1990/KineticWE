# MasterStack Ghost Window & Crash Bug

## Bug Description

With Master Stack tiling layout:
1. Have 3 windows open: master on left, 2 windows stacked on right
2. Meta+drag one of the stacked windows to another monitor
3. A **ghost window** (empty tile outline) remains on the original monitor
4. If you drag the window back to where the ghost is, the **session crashes**

## Root Cause

**`MasterStackLayoutEngine::cancelMoveWindow()`** removes the leaf from `m_leaves` and destroys it, but **does not remove the corresponding weight** from `m_masterWeights` / `m_stackWeights`. This leaves the two parallel lists out of sync.

### Before the drag (3 windows on OutputA):

```
m_leaves        = [leafW1, leafW2, leafW3]    ← W1=master, W2=stack0, W3=stack1
m_masterWeights = [1.0]
m_stackWeights  = [1.0, 1.0]
```

### After `cancelMoveWindow(W2)` (current broken code):

```
m_leaves        = [leafW1, leafW3]  ← 2 items
m_masterWeights = [1.0]             ← 1 item  ✓
m_stackWeights  = [1.0, 1.0]       ← 2 items  ✗ STALE! Should be [1.0]
```

### The crash

`reflow()` is called after the removal. It computes `stackCount = m_stackWeights.count()` which is **2**, then iterates:

```cpp
for (int i = 0; i < stackCount; ++i) {          // stackCount=2
    if (CustomTile *leaf = m_leaves[masters + i]) // i=1 → m_leaves[1+1] = m_leaves[2]
```

`m_leaves` only has 2 elements (indices 0,1), so `m_leaves[2]` is an **out-of-bounds access** — undefined behavior that causes the crash.

### Why `removeWindow` works but `cancelMoveWindow` doesn't

`removeWindow` (called for keyboard shortcut moves, desktop changes, etc.) **correctly** cleans up weights:

```cpp
void MasterStackLayoutEngine::removeWindow(Window *window) {
    const int idx = indexOfWindow(window);
    // ...
    const int masterCount = m_masterWeights.count();
    if (idx < masterCount) {
        m_masterWeights.removeAt(idx);                  // ✓ cleans up
    } else {
        m_stackWeights.removeAt(idx - masterCount);     // ✓ cleans up
    }
    // ... destroy leaf, reflow
}
```

`cancelMoveWindow` (called when a window is dragged to another output) does **not**:

```cpp
void MasterStackLayoutEngine::cancelMoveWindow(Window *window) {
    // ... finds sourceLeaf ...
    if (sourceLeaf->windows().isEmpty() || sourceLeaf->windows().contains(window)) {
        if (sourceLeaf->windows().contains(window)) {
            sourceLeaf->unmanage(window);
        }
        m_leaves.removeOne(sourceLeaf);     // ← removes leaf but NOT its weight
        m_root->destroyChild(sourceLeaf);
        reflow();                            // ← crashes because counts are out of sync
    }
}
```

## The Fix

Add weight cleanup to `cancelMoveWindow` — the exact same pattern `removeWindow` already uses.

### File: `src/tiles/masterstacklayoutengine.cpp`

In `cancelMoveWindow`, find the leaf's index before removal and use it to remove the matching weight:

```cpp
void MasterStackLayoutEngine::cancelMoveWindow(Window *window)
{
    QPointer<CustomTile> sourceLeaf = m_moveSourceLeaf;
    m_moveSourceLeaf.clear();
    m_moveSourceIndex = -1;

    if (!sourceLeaf || !m_leaves.contains(sourceLeaf)) {
        return;
    }

    if (sourceLeaf->windows().isEmpty() || sourceLeaf->windows().contains(window)) {
        if (sourceLeaf->windows().contains(window)) {
            sourceLeaf->unmanage(window);
        }

        // *** FIX: Remove the weight corresponding to this leaf first ***
        const int idx = m_leaves.indexOf(sourceLeaf);
        const int masterCount = m_masterWeights.count();
        if (idx < masterCount) {
            m_masterWeights.removeAt(idx);
        } else {
            m_stackWeights.removeAt(idx - masterCount);
        }

        m_leaves.removeOne(sourceLeaf);
        m_root->destroyChild(sourceLeaf);
        reflow();  // now safe: leaves and weights are in sync
    }
}
```

### What the "ghost window" is

The ghost window is a **secondary symptom** of the same bug: `reflow()` with stale weights computes geometry for phantom slots (out-of-bounds leaf access). The undefined behavior can leave visual artifacts — e.g., a tile rectangle drawn at a stale position on the original output — that the user sees as a "ghost." Once the lists go out of sync, any subsequent operation (like dragging the window back) accesses corrupted state and crashes.

### Verification

After the fix, verify that:

1. Dragging a stacked window to another monitor removes it cleanly from the source layout (the other two windows reflow to fill the gap, no ghost)
2. Dragging the window back to the original monitor re-inserts it without crashing
3. `m_masterWeights.count() + m_stackWeights.count() == m_leaves.count()` is always true after any operation

### Related code

The same pattern exists in other layout engines. Check the `cancelMoveWindow` implementation in each engine for the same weight-cleanup omission.

| Engine | File | Has the same bug? |
|--------|------|-------------------|
| MasterStackLayoutEngine | `src/tiles/masterstacklayoutengine.cpp` | ✅ **Yes** (this bug) |
| StackedLayoutEngine | `src/tiles/stackedlayoutengine.cpp` | Need to check |
| CenterTileLayoutEngine | `src/tiles/centertilelayoutengine.cpp` | Need to check |
| AutoGridLayoutEngine | `src/tiles/autogridlayoutengine.cpp` | Need to check |
| MonocleLayoutEngine | `src/tiles/monoclelayoutengine.cpp` | Need to check |
