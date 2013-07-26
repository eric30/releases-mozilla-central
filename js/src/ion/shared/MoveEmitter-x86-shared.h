/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ion_MoveEmitter_x86_shared_h
#define ion_MoveEmitter_x86_shared_h

#include "ion/IonMacroAssembler.h"
#include "ion/MoveResolver.h"

namespace js {
namespace ion {

class CodeGenerator;

class MoveEmitterX86
{
    typedef MoveResolver::Move Move;
    typedef MoveResolver::MoveOperand MoveOperand;

    bool inCycle_;
    MacroAssemblerSpecific &masm;

    // Original stack push value.
    uint32_t pushedAtStart_;

    // This is a store stack offset for the cycle-break spill slot, snapshotting
    // codegen->framePushed_ at the time it is allocated. -1 if not allocated.
    int32_t pushedAtCycle_;

    void assertDone();
    Operand cycleSlot();
    Operand toOperand(const MoveOperand &operand) const;
    Operand toPopOperand(const MoveOperand &operand) const;

    size_t characterizeCycle(const MoveResolver &moves, size_t i,
                             bool *allGeneralRegs, bool *allFloatRegs);
    bool maybeEmitOptimizedCycle(const MoveResolver &moves, size_t i,
                                 bool allGeneralRegs, bool allFloatRegs, size_t swapCount);
    void emitGeneralMove(const MoveOperand &from, const MoveOperand &to);
    void emitDoubleMove(const MoveOperand &from, const MoveOperand &to);
    void breakCycle(const MoveOperand &to, Move::Kind kind);
    void completeCycle(const MoveOperand &to, Move::Kind kind);

  public:
    MoveEmitterX86(MacroAssemblerSpecific &masm);
    ~MoveEmitterX86();
    void emit(const MoveResolver &moves);
    void finish();
};

typedef MoveEmitterX86 MoveEmitter;

} // ion
} // js

#endif /* ion_MoveEmitter_x86_shared_h */
