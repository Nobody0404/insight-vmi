
#include "astexpression.h"
#include "expressionevalexception.h"
#include "instance.h"
#include "structured.h"


const char* expressionTypeToString(ExpressionType type)
{
    switch (type) {
    case etNull:                return "etNull";
    case etVoid:                return "etVoid";
    case etUndefined:           return "etUndefined";
    case etRuntimeDependent:    return "etRuntimeDependent";
    case etLiteralConstant:     return "etLiteralConstant";
    case etEnumerator:          return "etEnumerator";
    case etVariable:            return "etVariable";
    case etLogicalOr:           return "etLogicalOr";
    case etLogicalAnd:          return "etLogicalAnd";
    case etInclusiveOr:         return "etInclusiveOr";
    case etExclusiveOr:         return "etExclusiveOr";
    case etAnd:                 return "etAnd";
    case etEquality:            return "etEquality";
    case etUnequality:          return "etUnequality";
    case etRelationalGE:        return "etRelationalGE";
    case etRelationalGT:        return "etRelationalGT";
    case etRelationalLE:        return "etRelationalLE";
    case etRelationalLT:        return "etRelationalLT";
    case etShiftLeft:           return "etShiftLeft";
    case etShiftRight:          return "etShiftRight";
    case etAdditivePlus:        return "etAdditivePlus";
    case etAdditiveMinus:       return "etAdditiveMinus";
    case etMultiplicativeMult:  return "etMultiplicativeMult";
    case etMultiplicativeDiv:   return "etMultiplicativeDiv";
    case etMultiplicativeMod:   return "etMultiplicativeMod";
    case etUnaryDec:            return "etUnaryDec";
    case etUnaryInc:            return "etUnaryInc";
    case etUnaryStar:           return "etUnaryStar";
    case etUnaryAmp:            return "etUnaryAmp";
    case etUnaryMinus:          return "etUnaryMinus";
    case etUnaryInv:            return "etUnaryInv";
    case etUnaryNot:            return "etUnaryNot";
    }
    return "UNKNOWN";
}


void ASTExpression::readFrom(KernelSymbolStream &in, SymFactory *factory)
{
    // Read alternative
    _alternative = fromStream(in, factory);
}


void ASTExpression::writeTo(KernelSymbolStream &out) const
{
    out << (qint32) type();
    toStream(_alternative, out);
}


ASTExpression* ASTExpression::fromStream(KernelSymbolStream &in,
                                         SymFactory *factory)
{
    ASTExpression* expr = 0;

    qint32 type;
    in >> type;

    // A null type means a null expression
    if (type) {
        expr = factory->createEmptyExpression((ExpressionType)type);
        expr->readFrom(in, factory);
    }

    return expr;
}


void ASTExpression::toStream(const ASTExpression* expr, KernelSymbolStream &out)
{
    if (expr)
        expr->writeTo(out);
    else
        out << (qint32)0;
}


void ASTConstantExpression::readFrom(KernelSymbolStream &in, SymFactory *factory)
{
    ASTExpression::readFrom(in, factory);
    in >> _value;
}


void ASTConstantExpression::writeTo(KernelSymbolStream &out) const
{
    ASTExpression::writeTo(out);
    out << _value;
}


void ASTEnumeratorExpression::readFrom(KernelSymbolStream &in, SymFactory *factory)
{
    ASTConstantExpression::readFrom(in, factory);
    in >> _valueSet;
    _symbol = 0;
}


void ASTEnumeratorExpression::writeTo(KernelSymbolStream &out) const
{
    ASTConstantExpression::writeTo(out);
    out << _valueSet;
}


QString ASTVariableExpression::toString(bool /*compact*/) const
{
    if (!_baseType)
        return "(undefined var. expr.)";

    return _transformations.toString(
                QString("(%1)").arg(_baseType->prettyName()));
}


bool ASTVariableExpression::equals(const ASTExpression *other) const
{
    if (!ASTExpression::equals(other))
        return false;
    const ASTVariableExpression* v =
            dynamic_cast<const ASTVariableExpression*>(other);
    // Compare base type based on their hash
    if (!v || !_baseType  || !v->_baseType ||
        _baseType->hash() != v->baseType()->hash())
        return false;

    // Compare transformations
    if (_transformations.size() != v->_transformations.size())
        return false;
    for (int i = 0; i < _transformations.size(); ++i) {
        if (_transformations[i].type != v->_transformations[i].type ||
            _transformations[i].arrayIndex != v->_transformations[i].arrayIndex ||
            _transformations[i].member != v->_transformations[i].member)
            return false;
    }

    return true;
}


ExpressionResult ASTVariableExpression::result(const Instance *inst) const
{
    // Make sure we got a valid instance and type
    if (!inst || !_baseType)
        return ExpressionResult(erUndefined);

    if (_transformations.isEmpty()) {
        // Check if the type hashes match.
        if (inst->type()->hash() == _baseType->hash() ||
            inst->type()->hash() ==
                _baseType->dereferencedBaseType(BaseType::trLexical)->hash())
            return inst->toExpressionResult();
        else
            exprEvalError(QString("Type hash of instance \"%1\" (0x%2) is different "
                                  "from our type \"%3\" (0x%4)")
                          .arg(inst->type()->prettyName())
                          .arg((uint)inst->type()->id(), 0, 16)
                          .arg(_baseType->prettyName())
                          .arg((uint)_baseType->id(), 0, 16));
    }

    const BaseType* bt = _baseType;
    QString prettyType = QString("(%1)").arg(bt->prettyName());
    int i = 0, cnt = 0;

    // Skip the suffixes starting from our _baseType until we find the matching
    // type ID
    for (; bt && bt->hash() != inst->type()->hash() && i < _transformations.size(); ++i) {
        // Did we find the instance's ID? Or can we dereference the type?
        if (bt->hash() != inst->type()->hash() &&
            (bt->type() & BaseType::trLexical))
            bt = bt->dereferencedBaseType(BaseType::trLexical);
        // Check once more
        if (bt->hash() == inst->type()->hash())
            break;

        switch (_transformations[i].type) {
        case ttDereference:
        case ttArray:
            bt = bt->dereferencedBaseType(BaseType::trAny, 1, &cnt);
            // Make sure we followed a pointer, but don't be fuzzy for the first
            // type as the context type normally is not a pointer type anyway
            if (i > 0 && cnt != 1)
                exprEvalError(QString("Type \"%1\" (0x%2) is not a pointer.")
                              .arg(prettyType)
                              .arg((uint)bt->id(), 0, 16));
            break;

        case ttMember: {
            const Structured* s = dynamic_cast<const Structured*>(bt);
            if (!s)
                exprEvalError(QString("Type \"%1\" (0x%2) is not a structured "
                                      "type")
                              .arg(prettyType)
                              .arg((uint)bt->id(), 0, 16));
            const StructuredMember* m = s->findMember(_transformations[i].member);
            if (!m)
                exprEvalError(QString("Type \"%1\" has no member \"%2\"")
                              .arg(prettyType)
                              .arg(_transformations[i].member));
            bt = m->refType();
            prettyType += "." + _transformations[i].member;
            break;
        }

        default:
            exprEvalError(QString("Unhandled symbol transformation for \"%1\" "
                                  ": %2")
                          .arg(prettyType)
                          .arg(_transformations[i].typeString()));
        }
    }

    if (!bt || bt->hash() != inst->type()->hash()) {
        exprEvalError(QString("Type hash of instance \"%1\" (0x%2) is different "
                              "from type \"%3\" (0x%4) of expression \"%5\"")
                      .arg(inst->type()->prettyName())
                      .arg((uint)inst->type()->id(), 0, 16)
                      .arg(bt ? bt->prettyName() : QString("(null)"))
                      .arg((uint)(bt ? bt->id() : 0), 0, 16)
                      .arg(prettyType));
    }

    // Apply remaining suffixes
    Instance tmp(*inst);
    for (int j = i; j < _transformations.size() && tmp.isValid(); ++j) {
        switch (_transformations[j].type) {
        case ttDereference:
            // Dereference the instance exactly once
            tmp = tmp.dereference(BaseType::trLexicalAndPointers, 1, &cnt);
            // Make sure we followed a pointer, but don't be fuzzy for the first
            // type as the context type normally is not a pointer type anyway
            if ((j > 0 && cnt != 1) || !tmp.isValid())
                return ExpressionResult(erUndefined);
            break;

        case ttMember:
            tmp = tmp.findMember(_transformations[j].member, BaseType::trLexical);
            prettyType += "." + _transformations[i].member;
            break;

        case ttArray:
            tmp = tmp.arrayElem(_transformations[j].arrayIndex);
            prettyType += QString("[%1]").arg(_transformations[i].arrayIndex);
            break;

        default:
            exprEvalError(QString("Unhandled symbol transformation for \"%1\" "
                                  ": %2")
                          .arg(prettyType)
                          .arg(_transformations[i].typeString()));
        }
    }

    return tmp.toExpressionResult();
}


ASTExpression *ASTVariableExpression::clone(ASTExpressionList &list) const
{
    ASTVariableExpression* expr = cloneTempl<ASTVariableExpression>(list);
    // Set nodes to null
    for (int i = 0; i < expr->_transformations.size(); ++i)
        expr->_transformations[i].node = 0;
    expr->_transformations.setTypeEvaluator(0);

    return expr;
}


void ASTVariableExpression::appendTransformation(SymbolTransformationType type)
{
    _transformations.append(type, 0);
}


void ASTVariableExpression::appendTransformation(const QString &member)
{
    _transformations.append(member, 0);
}


void ASTVariableExpression::appendTransformation(int arrayIndex)
{
    _transformations.append(arrayIndex, 0);
}


void ASTVariableExpression::readFrom(KernelSymbolStream &in, SymFactory *factory)
{
    ASTExpression::readFrom(in, factory);
    qint32 id;
    in >> id >> _global >> _transformations;
    _transformations.setTypeEvaluator(0);
    _baseType = factory->findBaseTypeById(id);
    // Make sure we found the type
    if (id && !_baseType)
        genericError(QString("Cannot find base type with ID 0x%1 in factory!")
                     .arg((uint)id, 0, 16));
}


void ASTVariableExpression::writeTo(KernelSymbolStream &out) const
{
    ASTExpression::writeTo(out);
    out << (qint32)(_baseType ? _baseType->id() : 0)
        << _global
        << _transformations;
}


ExpressionResult ASTBinaryExpression::result(const Instance *inst) const
{
    if (!_left || !_right)
        return ExpressionResult();

    ExpressionResult lr = _left->result(inst);
    ExpressionResult rr = _right->result(inst);
    ExpressionResult ret(lr.resultType | rr.resultType);
    ExpressionResultSize target = ret.size = binaryExprSize(lr, rr);
    // Is the expression decidable?
    if (ret.resultType & (erRuntime|erUndefined)) {
        // Undecidable, so return the combined result type
        ret.resultType |= erUndefined;
        return ret;
    }

    bool isUnsigned = (ret.size & esUnsigned) ? true : false;

#define LOGICAL_OP(op) \
    do { \
        ret.size = esUInt8; \
        if (target & esInteger) { \
            if (isUnsigned) \
                ret.result.i32 = (lr.uvalue(target) op rr.uvalue(target)) ? \
                    1 : 0; \
            else \
                ret.result.i32 = (lr.value(target) op rr.value(target)) ? \
                    1 : 0; \
        } \
        else if (target & esFloat) \
            ret.result.i32 = (lr.fvalue() op rr.fvalue()) ? 1 : 0; \
        else if (target & esDouble) \
            ret.result.i32 = (lr.dvalue() op rr.dvalue()) ? 1 : 0; \
        else \
            exprEvalError(QString("Invalid target type: %1") \
                          .arg(target)); \
    } while (0)


#define BITWISE_LOGICAL_OP(op) \
    do { \
        if (target & esInteger) { \
            if (isUnsigned) \
                ret.result.ui64 = lr.uvalue(target) op rr.uvalue(target); \
            else \
                ret.result.i64 = lr.value(target) op rr.value(target); \
        } \
        else \
            exprEvalError(QString("Invalid operator \"%1\" for target type %2.") \
                          .arg(#op) \
                          .arg(target)); \
    } while (0)


#define SHIFT_OP(op) \
    do { \
        ret.size = lr.size; /* result is the type of the left hand */  \
        if (lr.size & esInteger) { \
            /* If right-hand operand is negative, the result is undefined. */  \
            if (lr.size & esUnsigned)  \
                ret.result.ui64 = lr.uvalue() op rr.value(); \
            else \
                ret.result.i64 = lr.value() op rr.value(); \
        } \
        else \
            exprEvalError(QString("Invalid operator \"%1\" for target type %2.") \
                          .arg(#op) \
                          .arg(lr.size)); \
    } while (0)

#define ADDITIVE_OP(op) \
    do { \
        if (target & esInteger) { \
            if (isUnsigned) \
                ret.result.ui64 = lr.uvalue(target) op rr.uvalue(target); \
            else \
                ret.result.i64 = lr.value(target) op rr.value(target); \
        } \
        else if (target & esFloat) \
            ret.result.f = lr.fvalue() op rr.fvalue(); \
        else if (target & esDouble) \
            ret.result.d = lr.dvalue() op rr.dvalue(); \
        else \
            exprEvalError(QString("Invalid target type: %1") \
                          .arg(target)); \
    } while (0)

#define MULTIPLICATIVE_OP(op) \
    do { \
        if (target & esInteger) { \
            if (lr.size & esUnsigned) { \
                if (rr.size & esUnsigned) \
                    ret.result.i64 = lr.uvalue(target) op rr.uvalue(target); \
                else \
                    ret.result.i64 = lr.uvalue(target) op rr.value(target); \
            } \
            else { \
                if (rr.size & esUnsigned) \
                    ret.result.i64 = lr.value(target) op rr.uvalue(target); \
                else \
                    ret.result.i64 = lr.value(target) op rr.value(target); \
            } \
        } \
        else if (target & esFloat) \
            ret.result.f = lr.fvalue() op rr.fvalue(); \
        else if (target & esDouble) \
            ret.result.d = lr.dvalue() op rr.dvalue(); \
        else \
            exprEvalError(QString("Invalid target type: %1") \
                          .arg(target)); \
    } while (0)

#define MULTIPLICATIVE_MOD(op) \
    do { \
        if (target & esInteger) { \
            if (lr.size & esUnsigned) { \
                if (rr.size & esUnsigned) \
                    ret.result.i64 = lr.uvalue(target) op rr.uvalue(target); \
                else \
                    ret.result.i64 = lr.uvalue(target) op rr.value(target); \
            } \
            else { \
                if (rr.size & esUnsigned) \
                    ret.result.i64 = lr.value(target) op rr.uvalue(target); \
                else \
                    ret.result.i64 = lr.value(target) op rr.value(target); \
            } \
        } \
        else \
            exprEvalError(QString("Invalid operator \"%1\" for target type %2.") \
                          .arg(#op) \
                          .arg(target)); \
    } while (0)


    switch (_type) {
    case etLogicalOr:
        LOGICAL_OP(||);
        break;

    case etLogicalAnd:
        LOGICAL_OP(&&);
        break;

    case etInclusiveOr:
        BITWISE_LOGICAL_OP(|);
        break;

    case etExclusiveOr:
        BITWISE_LOGICAL_OP(^);
        break;

    case etAnd:
        BITWISE_LOGICAL_OP(&);
        break;

    case etEquality:
        LOGICAL_OP(==);
        break;

    case etUnequality:
        LOGICAL_OP(!=);
        break;

    case etRelationalGE:
        LOGICAL_OP(>=);
        break;

    case etRelationalGT:
        LOGICAL_OP(>);
        break;

    case etRelationalLE:
        LOGICAL_OP(<=);
        break;

    case etRelationalLT:
        LOGICAL_OP(<);
        break;

    case etShiftLeft:
        SHIFT_OP(<<);
        break;

    case etShiftRight:
        SHIFT_OP(>>);
        break;

    case etAdditivePlus:
        ADDITIVE_OP(+);
        break;

    case etAdditiveMinus:
        ADDITIVE_OP(-);
        break;

    case etMultiplicativeMult:
        MULTIPLICATIVE_OP(*);
        break;

    case etMultiplicativeDiv:
        MULTIPLICATIVE_OP(/);
        break;

    case etMultiplicativeMod:
        MULTIPLICATIVE_MOD(%);
        break;

    default:
        ret.resultType = erUndefined;
    }

    return ret;
}



QString ASTBinaryExpression::operatorToString() const
{
    switch (_type) {
    case etLogicalOr:           return "||";
    case etLogicalAnd:          return "&&";
    case etInclusiveOr:         return "|";
    case etExclusiveOr:         return "^";
    case etAnd:                 return "&";
    case etEquality:            return "==";
    case etUnequality:          return "!=";
    case etRelationalGE:        return ">=";
    case etRelationalGT:        return ">";
    case etRelationalLE:        return "<=";
    case etRelationalLT:        return "<";
    case etShiftLeft:           return "<<";
    case etShiftRight:          return ">>";
    case etAdditivePlus:        return "+";
    case etAdditiveMinus:       return "-";
    case etMultiplicativeMult:  return "*";
    case etMultiplicativeDiv:   return "/";
    case etMultiplicativeMod:   return "%";
    default:                    return "??";
    }
}


QString ASTBinaryExpression::toString(bool compact) const
{
    if (compact) {
        ExpressionResult res = result();
        // Does the expression evaluate to a constant value?
        if (!(res.resultType & (erRuntime|erUndefined|erGlobalVar|erLocalVar)))
            return res.toString();
    }

    QString left = _left ? _left->toString(compact) : QString("(left unset)");
    QString right = _right ? _right->toString(compact) : QString("(right unset)");

    return QString("(%1 %2 %3)").arg(left).arg(operatorToString()).arg(right);
}


ExpressionResultSize ASTBinaryExpression::binaryExprSize(
        const ExpressionResult& r1, const ExpressionResult& r2)
{
    /*
    Many binary operators that expect operands of arithmetic or enumeration
    type cause conversions and yield result types in a similar way. The
    purpose is to yield a common type, which is also the type of the result.
    This pattern is called the usual arithmetic conversions, which are
    defined as follows:
    - If either operand is of type long double, the other shall be converted
      to long double.
    - Otherwise, if either operand is double, the other shall be converted
      to double.
    - Otherwise, if either operand is float, the other shall be converted to
      float.
    - Otherwise, the integral promotions (4.5) shall be performed on both
      operands.
    - Then, if either operand is unsigned long the other shall be converted
      to unsigned long.
    - Otherwise, if one operand is a long int and the other unsigned int,
      then if a long int can represent all the values of an unsigned int,
      the unsigned int shall be converted to a long int; otherwise both
      operands shall be converted to unsigned long int.
    - Otherwise, if either operand is long, the other shall be converted to
      long.
    - Otherwise, if either operand is unsigned, the other shall be converted
      to unsigned. [Note: otherwise, the only remaining case is that both
      operands are int ]

    Integral promotions:
    An rvalue of type char, signed char, unsigned char, short int, or
    unsigned short int can be converted to an rvalue of type int if int can
    represent all the values of the source type; otherwise, the source
    rvalue can be converted to an rvalue of type unsigned int.
    */

    int r1r2_size = r1.size | r2.size;
    // If either is double, the result is double
    if (r1r2_size & esDouble)
        return esDouble;
    // Otherwise, if either is float, the result is float
    else if (r1r2_size & esFloat)
        return esFloat;
    // Otherwise
    else {
        // Integral promition
        ExpressionResultSize r1_size = (r1.size & (es8Bit|es16Bit)) ?
                    esInt32 : r1.size;
        ExpressionResultSize r2_size = (r2.size & (es8Bit|es16Bit)) ?
                    esInt32 : r2.size;
        r1r2_size = r1_size | r2_size;

        // If either is unsigned long, the result is unsigned long
        if (r1_size == esUInt64 || r2_size == esUInt64)
            return esUInt64;
        // Otherwise, if one is long and the other unsigned, result is long
        // Otherwise, if either is long, the result is long.
        else if (r1r2_size & esInt64)
            return esInt64;
        // Otherwise, if either is unsigned, the result is unsigned
        else if (r1r2_size & esUnsigned)
            return esUInt32;
    }
    return esInt32;
}


void ASTBinaryExpression::readFrom(KernelSymbolStream &in, SymFactory *factory)
{
    ASTExpression::readFrom(in, factory);
    _left = fromStream(in, factory);
    _right = fromStream(in, factory);
}


void ASTBinaryExpression::writeTo(KernelSymbolStream &out) const
{
    ASTExpression::writeTo(out);
    toStream(_left, out);
    toStream(_right, out);
}


ExpressionResult ASTUnaryExpression::result(const Instance *inst) const
{
    if (!_child)
        return ExpressionResult();

    ExpressionResult res = _child->result(inst);
    // Is the expression decidable?
    if (res.resultType & (erUndefined|erRuntime)) {
        /// @todo constants cannot be incremented, that makes no sense at all!
        // Undecidable, so return the combined result type
        res.resultType |= erUndefined;
        return res;
    }

#define UNARY_PREFIX(op) \
    do { \
        if (res.size & esInteger) { \
            if (res.size & es64Bit) \
                op res.result.ui64; \
            else if (res.size & es32Bit) \
                op res.result.ui32; \
            else if (res.size & es16Bit) \
                op res.result.ui16; \
            else \
                op res.result.ui8; \
        } \
        else \
            exprEvalError(QString("Invalid operator \"%1\" for target type %2.") \
                          .arg(#op) \
                          .arg(res.size)); \
    } while (0)


    switch (_type) {
    case etUnaryInc:
        UNARY_PREFIX(++);
        break;

    case etUnaryDec:
        UNARY_PREFIX(--);
        break;

        /// @todo Fixme
//        case etUnaryStar:
//        case etUnaryAmp:

    case etUnaryMinus:
        if (res.size & esInteger) {
            if (res.size & esUnsigned)
                res.result.ui64 = -res.result.ui64;
            else
                res.result.i64 = -res.result.i64;
        }
        else if (res.size & esDouble)
            res.result.d = -res.result.d;
        else if (res.size & esFloat)
            res.result.f = -res.result.f;
        else
            exprEvalError(QString("Invalid target type: %1").arg(res.size));
        break;

    case etUnaryInv:
        if (res.size & esInteger) {
            // Extend smaller values to 32 bit
            if (res.size & (es8Bit|es16Bit)) {
                res.result.i32 = ~res.value(esInt32);
                res.size = esInt32;
            }
            else {
                // I don't understand why, but GCC changes any int64 to unsigned
                // if the value was negative before, so just play the game...
                if (res.size == esInt64 && res.result.i64 < 0)
                    res.size = esUInt64;
                res.result.ui64 = ~res.result.ui64;
            }
        }
        else
            exprEvalError(QString("Invalid operator \"%1\" for target type %2")
                          .arg("~")
                          .arg(res.size));
        break;

    case etUnaryNot:
        if (res.size & esInteger)
            res.result.i32 = (!res.uvalue()) ? 1 : 0;
        else if (res.size & esDouble)
            res.result.i32 = (!res.dvalue()) ? 1 : 0;
        else if (res.size & esFloat)
            res.result.i32 = (!res.fvalue()) ? 1 : 0;
        else
            exprEvalError(QString("Invalid target type: %1").arg(res.size));
        res.size = esUInt8;
        break;

    default:
        exprEvalError(QString("Unhandled unary expression: %1")
                      .arg(expressionTypeToString(_type)));
    }

    return res;
}


QString ASTUnaryExpression::toString(bool compact) const
{
    if (compact) {
        ExpressionResult res = result();
        // Does the expression evaluate to a constant value?
        if (!(res.resultType & (erRuntime|erUndefined|erGlobalVar|erLocalVar)))
            return res.toString();
    }

    QString child = _child ? _child->toString(compact) : QString("(child unset)");

    return QString("%1%2").arg(operatorToString()).arg(child);
}


void ASTUnaryExpression::readFrom(KernelSymbolStream &in, SymFactory *factory)
{
    ASTExpression::readFrom(in, factory);
    _child = fromStream(in, factory);
}


void ASTUnaryExpression::writeTo(KernelSymbolStream &out) const
{
    ASTExpression::writeTo(out);
    toStream(_child, out);
}


QString ASTUnaryExpression::operatorToString() const
{
    switch (_type) {
    case etUnaryInc:   return "++";
    case etUnaryDec:   return "--";
    case etUnaryMinus: return "-";
    case etUnaryInv:   return "~";
    case etUnaryNot:   return "!";
    case etUnaryStar:  return "*";
    case etUnaryAmp:   return "&";
    default:           return "??";
    }
}
