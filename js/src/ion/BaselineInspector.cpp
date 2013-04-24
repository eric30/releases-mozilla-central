/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BaselineIC.h"
#include "BaselineInspector.h"

using namespace js;
using namespace js::ion;

bool
SetElemICInspector::sawOOBTypedArrayWrite() const
{
    if (!icEntry_)
        return false;

    // Check for SetElem_TypedArray stubs with expectOutOfBounds set.
    for (ICStub *stub = icEntry_->firstStub(); stub; stub = stub->next()) {
        if (!stub->isSetElem_TypedArray())
            continue;
        if (stub->toSetElem_TypedArray()->expectOutOfBounds())
            return true;
    }
    return false;
}

RawShape
BaselineInspector::maybeMonomorphicShapeForPropertyOp(jsbytecode *pc)
{
    if (!hasBaselineScript())
        return NULL;

    JS_ASSERT(isValidPC(pc));
    const ICEntry &entry = icEntryFromPC(pc);

    ICStub *stub = entry.firstStub();
    ICStub *next = stub->next();

    if (!next || !next->isFallback())
        return NULL;

    if (stub->isGetProp_Native()) {
        JS_ASSERT(next->isGetProp_Fallback());
        if (next->toGetProp_Fallback()->hadUnoptimizableAccess())
            return NULL;
        return stub->toGetProp_Native()->shape();
    }

    if (stub->isSetProp_Native()) {
        JS_ASSERT(next->isSetProp_Fallback());
        if (next->toSetProp_Fallback()->hadUnoptimizableAccess())
            return NULL;
        return stub->toSetProp_Native()->shape();
    }

    return NULL;
}

ICStub::Kind
BaselineInspector::monomorphicStubKind(jsbytecode *pc)
{
    if (!hasBaselineScript())
        return ICStub::INVALID;

    const ICEntry &entry = icEntryFromPC(pc);

    ICStub *stub = entry.firstStub();
    ICStub *next = stub->next();

    if (!next || !next->isFallback())
        return ICStub::INVALID;

    return stub->kind();
}

bool
BaselineInspector::dimorphicStubKind(jsbytecode *pc, ICStub::Kind *pfirst, ICStub::Kind *psecond)
{
    if (!hasBaselineScript())
        return false;

    const ICEntry &entry = icEntryFromPC(pc);

    ICStub *stub = entry.firstStub();
    ICStub *next = stub->next();
    ICStub *after = next ? next->next() : NULL;

    if (!after || !after->isFallback())
        return false;

    *pfirst = stub->kind();
    *psecond = next->kind();
    return true;
}

MIRType
BaselineInspector::expectedResultType(jsbytecode *pc)
{
    // Look at the IC entries for this op to guess what type it will produce,
    // returning MIRType_None otherwise.

    ICStub::Kind kind = monomorphicStubKind(pc);

    switch (kind) {
      case ICStub::BinaryArith_Int32:
      case ICStub::BinaryArith_BooleanWithInt32:
      case ICStub::UnaryArith_Int32:
        return MIRType_Int32;
      case ICStub::BinaryArith_Double:
      case ICStub::BinaryArith_DoubleWithInt32:
      case ICStub::UnaryArith_Double:
        return MIRType_Double;
      case ICStub::BinaryArith_StringConcat:
      case ICStub::BinaryArith_StringObjectConcat:
        return MIRType_String;
      default:
        return MIRType_None;
    }
}

// Whether a baseline stub kind is suitable for a double comparison that
// converts its operands to doubles.
static bool
CanUseDoubleCompare(ICStub::Kind kind)
{
    return kind == ICStub::Compare_Double || kind == ICStub::Compare_NumberWithUndefined;
}

MCompare::CompareType
BaselineInspector::expectedCompareType(jsbytecode *pc)
{
    ICStub::Kind kind = monomorphicStubKind(pc);

    if (CanUseDoubleCompare(kind))
        return MCompare::Compare_Double;

    ICStub::Kind first, second;
    if (dimorphicStubKind(pc, &first, &second)) {
        if (CanUseDoubleCompare(first) && CanUseDoubleCompare(second))
            return MCompare::Compare_Double;
    }

    return MCompare::Compare_Unknown;
}
