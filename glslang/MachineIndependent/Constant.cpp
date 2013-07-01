//
//Copyright (C) 2002-2005  3Dlabs Inc. Ltd.
//Copyright (C) 2012-2013 LunarG, Inc.
//
//All rights reserved.
//
//Redistribution and use in source and binary forms, with or without
//modification, are permitted provided that the following conditions
//are met:
//
//    Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
//    Redistributions in binary form must reproduce the above
//    copyright notice, this list of conditions and the following
//    disclaimer in the documentation and/or other materials provided
//    with the distribution.
//
//    Neither the name of 3Dlabs Inc. Ltd. nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
//THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
//FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
//COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
//ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//POSSIBILITY OF SUCH DAMAGE.
//

#include "localintermediate.h"
#include "math.h"
#include <cfloat>
#include <cstdlib>

namespace {

// Some helper functions

const double pi = 3.1415926535897932384626433832795;

bool CompareStruct(const TType& leftNodeType, constUnion* rightUnionArray, constUnion* leftUnionArray);

bool CompareStructure(const TType& leftNodeType, constUnion* rightUnionArray, constUnion* leftUnionArray)
{
    if (leftNodeType.isArray()) {
        TType typeWithoutArrayness(leftNodeType);
        typeWithoutArrayness.dereference();

        int arraySize = leftNodeType.getArraySize();

        for (int i = 0; i < arraySize; ++i) {
            int offset = typeWithoutArrayness.getObjectSize() * i;
            if (! CompareStruct(typeWithoutArrayness, &rightUnionArray[offset], &leftUnionArray[offset]))
                return false;
        }
    } else
        return CompareStruct(leftNodeType, rightUnionArray, leftUnionArray);

    return true;
}

bool CompareStruct(const TType& leftNodeType, constUnion* rightUnionArray, constUnion* leftUnionArray)
{
    TTypeList* fields = leftNodeType.getStruct();

    size_t structSize = fields->size();
    int index = 0;

    for (size_t j = 0; j < structSize; j++) {
        int size = (*fields)[j].type->getObjectSize();
        for (int i = 0; i < size; i++) {
            if ((*fields)[j].type->getBasicType() == EbtStruct) {
                if (!CompareStructure(*(*fields)[j].type, &rightUnionArray[index], &leftUnionArray[index]))
                    return false;
            } else {
                if (leftUnionArray[index] != rightUnionArray[index])
                    return false;
                index++;
            }

        }
    }
    return true;
}

}; // end anonymous namespace

//
// The fold functions see if an operation on a constant can be done in place,
// without generating run-time code.
//
// Returns the node to keep using, which may or may not be the node passed in.
//
// Note: As of version 1.2, all constant operations must be folded.  It is
// not opportunistic, but rather a semantic requirement.
//

//
// Do folding between a pair of nodes
//
TIntermTyped* TIntermConstantUnion::fold(TOperator op, TIntermTyped* constantNode, TInfoSink& infoSink)
{
    constUnion *unionArray = getUnionArrayPointer();
    int objectSize = getType().getObjectSize();
    constUnion* newConstArray = 0;

    // For most cases, the return type matches the argument type, so set that
    // up and just code to exceptions below.
    TType returnType(getType());

    //
    // A pair of nodes is to be folded together
    //

    TIntermConstantUnion *node = constantNode->getAsConstantUnion();
    constUnion *rightUnionArray = node->getUnionArrayPointer();

    if (constantNode->getType().getObjectSize() == 1 && objectSize > 1) {
        // for a case like float f = vec4(2,3,4,5) + 1.2;
        rightUnionArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; ++i)
            rightUnionArray[i] = *node->getUnionArrayPointer();
    } else if (constantNode->getType().getObjectSize() > 1 && objectSize == 1) {
        // for a case like float f = 1.2 + vec4(2,3,4,5);
        rightUnionArray = node->getUnionArrayPointer();
        unionArray = new constUnion[constantNode->getType().getObjectSize()];
        for (int i = 0; i < constantNode->getType().getObjectSize(); ++i)
            unionArray[i] = *getUnionArrayPointer();
        returnType = node->getType();
        objectSize = constantNode->getType().getObjectSize();
    }

    int index = 0;
    bool boolNodeFlag = false;
    switch(op) {
    case EOpAdd:
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++)
            newConstArray[i] = unionArray[i] + rightUnionArray[i];
        break;
    case EOpSub:
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++)
            newConstArray[i] = unionArray[i] - rightUnionArray[i];
        break;

    case EOpMul:
    case EOpVectorTimesScalar:
    case EOpMatrixTimesScalar:
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++)
            newConstArray[i] = unionArray[i] * rightUnionArray[i];
        break;
    case EOpMatrixTimesMatrix:
        newConstArray = new constUnion[getMatrixRows() * node->getMatrixCols()];
        for (int row = 0; row < getMatrixRows(); row++) {
            for (int column = 0; column < node->getMatrixCols(); column++) {
                double sum = 0.0f;
                for (int i = 0; i < node->getMatrixRows(); i++)
                    sum += unionArray[i * getMatrixRows() + row].getDConst() * rightUnionArray[column * node->getMatrixRows() + i].getDConst();
                newConstArray[column * getMatrixRows() + row].setDConst(sum);
            }
        }
        returnType = TType(getType().getBasicType(), EvqConst, 0, getMatrixRows(), node->getMatrixCols());
        break;
    case EOpDiv:
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++) {
            switch (getType().getBasicType()) {
            case EbtFloat:
                if (rightUnionArray[i] == 0.0f) {
                    newConstArray[i].setDConst(FLT_MAX);  // TODO: double support
                } else
                    newConstArray[i].setDConst(unionArray[i].getDConst() / rightUnionArray[i].getDConst());
            break;

            case EbtInt:
                if (rightUnionArray[i] == 0) {
                    newConstArray[i].setIConst(0xEFFFFFFF);
                } else
                    newConstArray[i].setIConst(unionArray[i].getIConst() / rightUnionArray[i].getIConst());
                break;

            case EbtUint:
                if (rightUnionArray[i] == 0) {
                    newConstArray[i].setUConst(0xFFFFFFFF);
                } else
                    newConstArray[i].setUConst(unionArray[i].getUConst() / rightUnionArray[i].getUConst());
                break;
            default:
                infoSink.info.message(EPrefixInternalError, "Constant folding cannot be done for \"/\"", getLine());
                return 0;
            }
        }
        break;

    case EOpMatrixTimesVector:
        newConstArray = new constUnion[getMatrixRows()];
        for (int i = 0; i < getMatrixRows(); i++) {
            double sum = 0.0f;
            for (int j = 0; j < node->getVectorSize(); j++) {
                sum += unionArray[j*getMatrixRows() + i].getDConst() * rightUnionArray[j].getDConst();
            }
            newConstArray[i].setDConst(sum);
        }

        returnType = TType(getBasicType(), EvqConst, getMatrixRows());
        break;

    case EOpVectorTimesMatrix:
        newConstArray = new constUnion[node->getMatrixCols()];
        for (int i = 0; i < node->getMatrixCols(); i++) {
            double sum = 0.0f;
            for (int j = 0; j < getVectorSize(); j++)
                sum += unionArray[j].getDConst() * rightUnionArray[i*node->getMatrixRows() + j].getDConst();
            newConstArray[i].setDConst(sum);
        }

        returnType = TType(getBasicType(), EvqConst, node->getMatrixCols());
        break;

    case EOpMod:
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++)
            newConstArray[i] = unionArray[i] % rightUnionArray[i];
        break;

    case EOpRightShift:
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++)
            newConstArray[i] = unionArray[i] >> rightUnionArray[i];
        break;

    case EOpLeftShift:
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++)
            newConstArray[i] = unionArray[i] << rightUnionArray[i];
        break;

    case EOpAnd:
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++)
            newConstArray[i] = unionArray[i] & rightUnionArray[i];
        break;
    case EOpInclusiveOr:
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++)
            newConstArray[i] = unionArray[i] | rightUnionArray[i];
        break;
    case EOpExclusiveOr:
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++)
            newConstArray[i] = unionArray[i] ^ rightUnionArray[i];
        break;

    case EOpLogicalAnd: // this code is written for possible future use, will not get executed currently
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++)
            newConstArray[i] = unionArray[i] && rightUnionArray[i];
        break;

    case EOpLogicalOr: // this code is written for possible future use, will not get executed currently
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++)
            newConstArray[i] = unionArray[i] || rightUnionArray[i];
        break;

    case EOpLogicalXor:
        newConstArray = new constUnion[objectSize];
        for (int i = 0; i < objectSize; i++) {
            switch (getType().getBasicType()) {
            case EbtBool: newConstArray[i].setBConst((unionArray[i] == rightUnionArray[i]) ? false : true); break;
            default: assert(false && "Default missing");
            }
        }
        break;

    case EOpLessThan:
        assert(objectSize == 1);
        newConstArray = new constUnion[1];
        newConstArray->setBConst(*unionArray < *rightUnionArray);
        returnType = TType(EbtBool, EvqConst);
        break;
    case EOpGreaterThan:
        assert(objectSize == 1);
        newConstArray = new constUnion[1];
        newConstArray->setBConst(*unionArray > *rightUnionArray);
        returnType = TType(EbtBool, EvqConst);
        break;
    case EOpLessThanEqual:
    {
        assert(objectSize == 1);
        constUnion constant;
        constant.setBConst(*unionArray > *rightUnionArray);
        newConstArray = new constUnion[1];
        newConstArray->setBConst(!constant.getBConst());
        returnType = TType(EbtBool, EvqConst);
        break;
    }
    case EOpGreaterThanEqual:
    {
        assert(objectSize == 1);
        constUnion constant;
        constant.setBConst(*unionArray < *rightUnionArray);
        newConstArray = new constUnion[1];
        newConstArray->setBConst(!constant.getBConst());
        returnType = TType(EbtBool, EvqConst);
        break;
    }

    case EOpEqual:
        if (getType().getBasicType() == EbtStruct) {
            if (! CompareStructure(node->getType(), node->getUnionArrayPointer(), unionArray))
                boolNodeFlag = true;
        } else {
            for (int i = 0; i < objectSize; i++) {
                if (unionArray[i] != rightUnionArray[i]) {
                    boolNodeFlag = true;
                    break;  // break out of for loop
                }
            }
        }

        newConstArray = new constUnion[1];
        newConstArray->setBConst(! boolNodeFlag);
        returnType = TType(EbtBool, EvqConst);
        break;

    case EOpNotEqual:
        if (getType().getBasicType() == EbtStruct) {
            if (CompareStructure(node->getType(), node->getUnionArrayPointer(), unionArray))
                boolNodeFlag = true;
        } else {
            for (int i = 0; i < objectSize; i++) {
                if (unionArray[i] == rightUnionArray[i]) {
                    boolNodeFlag = true;
                    break;  // break out of for loop
                }
            }
        }

        newConstArray = new constUnion[1];
        newConstArray->setBConst(! boolNodeFlag);
        returnType = TType(EbtBool, EvqConst);
        break;

    default:
        infoSink.info.message(EPrefixInternalError, "Invalid operator for constant folding", getLine());

        return 0;
    }

    TIntermConstantUnion *newNode = new TIntermConstantUnion(newConstArray, returnType);
    newNode->setLine(getLine());

    return newNode;
}

//
// Do single unary node folding
//
TIntermTyped* TIntermConstantUnion::fold(TOperator op, const TType& returnType, TInfoSink& infoSink)
{
    constUnion *unionArray = getUnionArrayPointer();
    int objectSize = getType().getObjectSize();

    // First, size the result, which is mostly the same as the argument's size,
    // but not always.
    constUnion* newConstArray;
    switch (op) {
    // TODO: functionality: constant folding: finish listing exceptions to size here
    case EOpDeterminant:
    case EOpAny:
    case EOpAll:
    case EOpLength:
        newConstArray = new constUnion[1];
        break;        
    
    default:
        newConstArray = new constUnion[objectSize];
    }

    // Process non-component-wise operations
    switch (op) {
    case EOpLength:
    case EOpNormalize:
    {
        double sum = 0;
        for (int i = 0; i < objectSize; i++)
            sum += double(unionArray[i].getDConst()) * unionArray[i].getDConst();
        double length = sqrt(sum);
        if (op == EOpLength)
            newConstArray[0].setDConst(length);
        else {
            for (int i = 0; i < objectSize; i++)
                newConstArray[i].setDConst(unionArray[i].getDConst() / length);
        }
        break;
    }
    default:
        break;
    }

    for (int i = 0; i < objectSize; i++) {
        switch (op) {
        case EOpNegative:
            switch (getType().getBasicType()) {
            case EbtFloat: newConstArray[i].setDConst(-unionArray[i].getDConst()); break;
            case EbtInt:   newConstArray[i].setIConst(-unionArray[i].getIConst()); break;
            case EbtUint:  newConstArray[i].setUConst(static_cast<unsigned int>(-static_cast<int>(unionArray[i].getUConst())));  break;
            default:
                infoSink.info.message(EPrefixInternalError, "Unary operation not folded into constant", getLine());
                return 0;
            }
            break;
        case EOpLogicalNot:
        case EOpVectorLogicalNot:
            switch (getType().getBasicType()) {
            case EbtBool:  newConstArray[i].setBConst(!unionArray[i].getBConst()); break;
            default:
                infoSink.info.message(EPrefixInternalError, "Unary operation not folded into constant", getLine());
                return 0;
            }
            break;
        case EOpBitwiseNot:
            newConstArray[i] = ~unionArray[i];
            break;
        case EOpRadians:
            newConstArray[i].setDConst(unionArray[i].getDConst() * pi / 180.0);
            break;
        case EOpDegrees:
            newConstArray[i].setDConst(unionArray[i].getDConst() * 180.0 / pi);
            break;
        case EOpSin:
            newConstArray[i].setDConst(sin(unionArray[i].getDConst()));
            break;
        case EOpCos:
            newConstArray[i].setDConst(cos(unionArray[i].getDConst()));
            break;
        case EOpTan:
            newConstArray[i].setDConst(tan(unionArray[i].getDConst()));
            break;
        case EOpAsin:
            newConstArray[i].setDConst(asin(unionArray[i].getDConst()));
            break;
        case EOpAcos:
            newConstArray[i].setDConst(acos(unionArray[i].getDConst()));
            break;
        case EOpAtan:
            newConstArray[i].setDConst(atan(unionArray[i].getDConst()));
            break;

        case EOpLength:
        case EOpNormalize:
            // handled above as special case
            break;

        case EOpDPdx:
        case EOpDPdy:
        case EOpFwidth:
            // The derivatives are all mandated to create a constant 0.
            newConstArray[i].setDConst(0.0);
            break;

        case EOpExp:
            newConstArray[i].setDConst(exp(unionArray[i].getDConst()));
            break;
        case EOpLog:
            newConstArray[i].setDConst(log(unionArray[i].getDConst()));
            break;
        case EOpExp2:
            {
                const double inv_log2_e = 0.69314718055994530941723212145818;
                newConstArray[i].setDConst(exp(unionArray[i].getDConst() * inv_log2_e));
                break;
            }
        case EOpLog2:
            {
                const double log2_e = 1.4426950408889634073599246810019;
                newConstArray[i].setDConst(log2_e * log(unionArray[i].getDConst()));
                break;
            }
        case EOpSqrt:
            newConstArray[i].setDConst(sqrt(unionArray[i].getDConst()));
            break;
        case EOpInverseSqrt:
            newConstArray[i].setDConst(1.0 / sqrt(unionArray[i].getDConst()));
            break;

        case EOpAbs:
            if (unionArray[i].getType() == EbtDouble)
                newConstArray[i].setDConst(abs(unionArray[i].getDConst()));
            else if (unionArray[i].getType() == EbtInt)
                newConstArray[i].setIConst(abs(unionArray[i].getIConst()));
            else
                newConstArray[i] = unionArray[i];
            break;
        case EOpSign:
            #define SIGN(X) (X == 0 ? 0 : (X < 0 ? -1 : 1))
            if (unionArray[i].getType() == EbtDouble)
                newConstArray[i].setDConst(SIGN(unionArray[i].getDConst()));
            else
                newConstArray[i].setIConst(SIGN(unionArray[i].getIConst()));
            break;
        case EOpFloor:
            newConstArray[i].setDConst(floor(unionArray[i].getDConst()));
            break;
        case EOpTrunc:
            if (unionArray[i].getDConst() > 0)
                newConstArray[i].setDConst(floor(unionArray[i].getDConst()));
            else
                newConstArray[i].setDConst(ceil(unionArray[i].getDConst()));
            break;
        case EOpRound:
            newConstArray[i].setDConst(floor(0.5 + unionArray[i].getDConst()));
            break;
        case EOpRoundEven:
        {
            double flr = floor(unionArray[i].getDConst());
            bool even = flr / 2.0 == floor(flr / 2.0);
            double rounded = even ? ceil(unionArray[i].getDConst() - 0.5) : floor(unionArray[i].getDConst() + 0.5);
            newConstArray[i].setDConst(rounded);
            break;
        }
        case EOpCeil:
            newConstArray[i].setDConst(ceil(unionArray[i].getDConst()));
            break;
        case EOpFract:
        {
            double x = unionArray[i].getDConst();
            newConstArray[i].setDConst(x - floor(x));
            break;
        }

        // TODO: Functionality: constant folding: the rest of the ops have to be fleshed out

        case EOpSinh:
        case EOpCosh:
        case EOpTanh:
        case EOpAsinh:
        case EOpAcosh:
        case EOpAtanh:

        case EOpIsNan:
        case EOpIsInf:

        case EOpFloatBitsToInt:
        case EOpFloatBitsToUint:
        case EOpIntBitsToFloat:
        case EOpUintBitsToFloat:
        case EOpPackSnorm2x16:
        case EOpUnpackSnorm2x16:
        case EOpPackUnorm2x16:
        case EOpUnpackUnorm2x16:
        case EOpPackHalf2x16:
        case EOpUnpackHalf2x16:

        case EOpDeterminant:
        case EOpMatrixInverse:
        case EOpTranspose:

        case EOpAny:
        case EOpAll:

        default:
            infoSink.info.message(EPrefixInternalError, "missing operator for unary constant folding", getLine());
            return 0;
        }
    }

    TIntermConstantUnion *newNode = new TIntermConstantUnion(newConstArray, returnType);
    newNode->getTypePointer()->getQualifier().storage = EvqConst;
    newNode->setLine(getLine());

    return newNode;
}

//
// Do constant folding for an aggregate node that has all its children
// as constants and an operator that requires constant folding.
// 
TIntermTyped* TIntermediate::fold(TIntermAggregate* aggrNode)
{
    if (! areAllChildConst(aggrNode))
        return aggrNode;

    if (aggrNode->isConstructor())
        return foldConstructor(aggrNode);

    TIntermSequence& children = aggrNode->getSequence();

    // First, see if this is an operation to constant fold, kick out if not,
    // see what size the result is if so.
    int objectSize;
    switch (aggrNode->getOp()) {
    case EOpMin:
    case EOpMax:
    case EOpReflect:
    case EOpRefract:
    case EOpFaceForward:
    case EOpAtan:
    case EOpPow:
    case EOpClamp:
    case EOpMix:
    case EOpDistance:
    case EOpCross:
        objectSize = children[0]->getAsConstantUnion()->getType().getObjectSize();
        break;
    case EOpDot:
        objectSize = 1;
        break;
    case EOpOuterProduct:
        objectSize = children[0]->getAsTyped()->getType().getVectorSize() *
                     children[1]->getAsTyped()->getType().getVectorSize();
        break;
    case EOpStep:
        objectSize = std::max(children[0]->getAsTyped()->getType().getVectorSize(),
                              children[1]->getAsTyped()->getType().getVectorSize());
        break;
    case EOpSmoothStep:
        objectSize = std::max(children[0]->getAsTyped()->getType().getVectorSize(),
                              children[2]->getAsTyped()->getType().getVectorSize());
        break;
    default:
        return aggrNode;
    }
    constUnion* newConstArray = new constUnion[objectSize];

    TVector<constUnion*> childConstUnions;
    for (unsigned int i = 0; i < children.size(); ++i)
        childConstUnions.push_back(children[i]->getAsConstantUnion()->getUnionArrayPointer());

    // Second, do the actual folding

    // TODO: Functionality: constant folding: separate component-wise from non-component-wise
    switch (aggrNode->getOp()) {
    case EOpMin:
    case EOpMax:
        for (int i = 0; i < objectSize; i++) {
            if (aggrNode->getOp() == EOpMax)
                newConstArray[i].setDConst(std::max(childConstUnions[0]->getDConst(), childConstUnions[1]->getDConst()));
            else
                newConstArray[i].setDConst(std::min(childConstUnions[0]->getDConst(), childConstUnions[1]->getDConst()));
        }
        break;

    // TODO: Functionality: constant folding: the rest of the ops have to be fleshed out

    case EOpAtan:
    case EOpPow:
    case EOpModf:
    case EOpClamp:
    case EOpMix:
    case EOpStep:
    case EOpSmoothStep:
    case EOpDistance:
    case EOpDot:
    case EOpCross:
    case EOpFaceForward:
    case EOpReflect:
    case EOpRefract:
    case EOpOuterProduct:
        infoSink.info.message(EPrefixInternalError, "constant folding operation not implemented", aggrNode->getLine());
        return aggrNode;

    default:
        return aggrNode;
    }

    TIntermConstantUnion *newNode = new TIntermConstantUnion(newConstArray, aggrNode->getType());
    newNode->getTypePointer()->getQualifier().storage = EvqConst;
    newNode->setLine(aggrNode->getLine());

    return newNode;
}

bool TIntermediate::areAllChildConst(TIntermAggregate* aggrNode)
{
    bool allConstant = true;

    // check if all the child nodes are constants so that they can be inserted into
    // the parent node
    if (aggrNode) {
        TIntermSequence& childSequenceVector = aggrNode->getSequence();
        for (TIntermSequence::iterator p  = childSequenceVector.begin();
                                       p != childSequenceVector.end(); p++) {
            if (!(*p)->getAsTyped()->getAsConstantUnion())
                return false;
        }
    }

    return allConstant;
}

TIntermTyped* TIntermediate::foldConstructor(TIntermAggregate* aggrNode)
{
    bool returnVal = false;

    constUnion* unionArray = new constUnion[aggrNode->getType().getObjectSize()];
    if (aggrNode->getSequence().size() == 1)
        returnVal = parseConstTree(aggrNode->getLine(), aggrNode, unionArray, aggrNode->getOp(), aggrNode->getType(), true);
    else
        returnVal = parseConstTree(aggrNode->getLine(), aggrNode, unionArray, aggrNode->getOp(), aggrNode->getType());

    if (returnVal)
        return aggrNode;

    return addConstantUnion(unionArray, aggrNode->getType(), aggrNode->getLine());
}