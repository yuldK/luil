/* luil 앱 수준 실행 취소 stack의 브라우저 이식 — 10강 실험용
 *
 * src/ui/undo_stack.cpp를 함수 단위로 옮겼다. 이름 옆 주석은 C++ 원본의 이름이다.
 * 메시지(undo·redo)는 아무 JS 값이고, null/undefined가 "빈 메시지"(app_message::empty)다.
 * 컴파일한 undo_stack.cpp와 무작위 연산열로 대조했다.
 */
(function () {
  "use strict";
  const DEFAULT_STEPS = 1000;               // undo_stack_default_steps
  const DEFAULT_BYTES = 32 * 1024 * 1024;   // undo_stack_default_bytes
  const emptyRecord = () => ({ label: "", undo: [], redo: [], mergeKey: "", weight: 0 });  // record

  class UndoStack {
    constructor(limits = {}) {
      this.limits = { steps: limits.steps ?? DEFAULT_STEPS, bytes: limits.bytes ?? DEFAULT_BYTES };
      this.undoSide = [];          // undo_: 앞이 가장 오래된 걸음
      this.redoSide = [];          // redo_: 끝이 다음에 다시 실행할 걸음
      this.bytesUsed = 0;          // bytes_: 되돌릴 쪽 weight의 합
      this.clean = 0;              // clean_: 저장 지점(되돌릴 수 있는 걸음 수). null이면 사라짐
      this.mergeArmed = false;     // merge_armed_
      this.macroDepth = 0;         // macro_depth_
      this.macro = emptyRecord();  // macro_
      this.replaying = false;      // replaying_
      this.lastEvent = "";         // 시각화용: 마지막 push가 어느 갈래였나 (원본에 없음)
    }
    push(step) {
      if (this.replaying || step.undo == null || step.redo == null) { this.lastEvent = this.replaying ? "막힘: 재생 중" : "막힘: 빈 메시지"; return false; }
      const key = step.mergeKey ?? "";
      const weight = step.weight ?? 0;
      if (this.macroDepth > 0) {
        this.macro.undo.push(step.undo);
        this.macro.redo.push(step.redo);
        this.macro.weight += weight;
        this.lastEvent = "묶음에 담음";
        return true;
      }
      const last = this.undoSide[this.undoSide.length - 1];
      if (this.mergeArmed && key !== "" && last !== undefined && last.mergeKey === key && this.isClean() === false) {
        const merged = Math.max(last.weight, weight);
        this.bytesUsed = this.bytesUsed - last.weight + merged;
        last.weight = merged;
        last.label = step.label ?? "";
        last.redo = [step.redo];
        this.trim();
        this.lastEvent = "앞 걸음에 합침";
        return true;
      }
      this.append({ label: step.label ?? "", undo: [step.undo], redo: [step.redo], mergeKey: key, weight });
      this.mergeArmed = key !== "";
      this.lastEvent = "새 걸음";
      return true;
    }
    begin(label) {
      if (this.replaying) return;
      this.mergeArmed = false;
      if (this.macroDepth++ === 0) { this.macro = emptyRecord(); this.macro.label = label; }
    }
    end() {
      if (this.replaying || this.macroDepth === 0) return false;
      if (--this.macroDepth > 0) return true;
      const entry = this.macro;
      this.macro = emptyRecord();
      if (entry.undo.length === 0) return true;
      this.append(entry);
      return true;
    }
    undo(apply) {
      if (this.replaying || this.macroDepth > 0 || typeof apply !== "function" || this.undoSide.length === 0) return false;
      const step = this.undoSide.pop();
      this.bytesUsed -= step.weight;
      this.redoSide.push(step);
      this.mergeArmed = false;
      this.replaying = true;
      try { for (let i = step.undo.length - 1; i >= 0; --i) apply(step.undo[i]); }
      finally { this.replaying = false; }
      return true;
    }
    redo(apply) {
      if (this.replaying || this.macroDepth > 0 || typeof apply !== "function" || this.redoSide.length === 0) return false;
      const step = this.redoSide.pop();
      this.bytesUsed += step.weight;
      this.undoSide.push(step);
      this.mergeArmed = false;
      this.trim();
      this.replaying = true;
      try { for (const m of step.redo) apply(m); }
      finally { this.replaying = false; }
      return true;
    }
    canUndo() { return this.macroDepth === 0 && this.undoSide.length > 0; }
    canRedo() { return this.macroDepth === 0 && this.redoSide.length > 0; }
    undoLabel() { return this.canUndo() ? this.undoSide[this.undoSide.length - 1].label : ""; }
    redoLabel() { return this.canRedo() ? this.redoSide[this.redoSide.length - 1].label : ""; }
    undoCount() { return this.undoSide.length; }
    redoCount() { return this.redoSide.length; }
    bytes() { return this.bytesUsed; }
    breakMerge() { if (!this.replaying) this.mergeArmed = false; }
    setClean() { if (this.replaying || this.macroDepth > 0) return; this.clean = this.undoSide.length; this.mergeArmed = false; }
    isClean() { return this.macro.undo.length === 0 && this.clean !== null && this.clean === this.undoSide.length; }
    clear() {
      if (this.replaying) return;
      this.undoSide = []; this.redoSide = []; this.bytesUsed = 0; this.clean = 0;
      this.mergeArmed = false; this.macroDepth = 0; this.macro = emptyRecord();
    }
    setLimits(limits) { if (this.replaying) return; this.limits = { steps: limits.steps, bytes: limits.bytes }; this.trim(); }
    append(entry) {
      if (this.clean !== null && this.clean > this.undoSide.length) this.clean = null;
      this.redoSide = [];
      this.bytesUsed += entry.weight;
      this.undoSide.push(entry);
      this.mergeArmed = false;
      this.trim();
    }
    trim() {
      const { steps, bytes } = this.limits;
      while (this.undoSide.length > 1 && ((steps !== 0 && this.undoSide.length > steps) || (bytes !== 0 && this.bytesUsed > bytes))) {
        const front = this.undoSide.shift();
        this.bytesUsed -= front.weight;
        if (this.clean !== null) this.clean = this.clean === 0 ? null : this.clean - 1;
      }
    }
  }

  // 그리기 전용 읽기 (stack을 바꾸지 않는다)
  function viewOf(stack) {
    const steps = [...stack.undoSide, ...[...stack.redoSide].reverse()];
    return {
      steps: steps.map((r, index) => ({ index, label: r.label, undo: r.undo, redo: r.redo, mergeKey: r.mergeKey, weight: r.weight, side: index < stack.undoSide.length ? "undo" : "redo" })),
      cursor: stack.undoSide.length,
      savePoint: stack.clean,
      clean: stack.isClean(),
      mergeArmed: stack.mergeArmed,
      macro: stack.macroDepth > 0 ? { depth: stack.macroDepth, label: stack.macro.label, messages: stack.macro.undo.length, weight: stack.macro.weight } : null,
      bytes: stack.bytes(),
      limits: { ...stack.limits },
      canUndo: stack.canUndo(), canRedo: stack.canRedo(), undoLabel: stack.undoLabel(), redoLabel: stack.redoLabel(),
    };
  }

  window.LuilUndo = { UndoStack, viewOf, DEFAULT_STEPS, DEFAULT_BYTES };
})();
