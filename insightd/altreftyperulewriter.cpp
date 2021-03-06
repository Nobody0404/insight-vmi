#include "altreftyperulewriter.h"
#include <insight/ioexception.h>
#include <insight/filenotfoundexception.h>
#include <insight/typeruleparser.h>
#include <insight/typefilter.h>
#include <insight/osfilter.h>
#include <insight/symfactory.h>
#include <insight/structured.h>
#include <insight/structuredmember.h>
#include <insight/refbasetype.h>
#include <insight/variable.h>
#include <insight/astexpression.h>
#include <insight/typerule.h>
#include <insight/typeruleexception.h>
#include <insight/console.h>
#include <debug.h>
#include <QXmlStreamWriter>
#include <QDir>
#include <QFile>
#include <QDateTime>

int AltRefTypeRuleWriter::_indentation = -1;
const QString AltRefTypeRuleWriter::_srcVar("src");


int AltRefTypeRuleWriter::write(const QString& name, const QString& baseDir)
{
    _symbolsDone = 0;
    _totalSymbols = _factory->types().size() + _factory->vars().size();
    operationStarted();

    _filesWritten.clear();
    const MemSpecs& specs = _factory->memSpecs();

    // Check if directories exist
    QDir dir(baseDir);
    QDir rulesDir(dir.absoluteFilePath(name));
    if (!dir.exists(name) && !dir.mkpath(name))
        ioError(QString("Error creating directory \"%1\".")
                    .arg(rulesDir.absolutePath()));

    QFile incFile(dir.absoluteFilePath(name + ".xml"));
    if (!incFile.open(QIODevice::WriteOnly|QIODevice::Truncate))
        ioError(QString("Error opening file \"%1\" for writing.")
                    .arg(incFile.fileName()));

    _filesWritten.append(QDir::cleanPath(incFile.fileName()));

    try {
        QXmlStreamWriter writer(&incFile);
        writer.setAutoFormatting(true);
        writer.setAutoFormattingIndent(_indentation);

        // Begin document with a comment including guest OS details
        writer.writeStartDocument();
        writeComment(writer, QString("\nFile created on %1\n%2")
                                .arg(QDateTime::currentDateTime().toString())
                                .arg(specs.toString()));

        // Begin knowledge file
        writer.writeStartElement(xml::typeknowledge); // typeknowledge
        writer.writeAttribute(xml::version, QString::number(xml::currentVer));

        // Write OS version information
        writer.writeAttribute(xml::os, specs.version.sysname);
        if (specs.arch == MemSpecs::ar_i386)
            writer.writeAttribute(xml::architecture, xml::arX86);
        else if (specs.arch == MemSpecs::ar_i386_pae)
            writer.writeAttribute(xml::architecture, xml::arX86PAE);
        else if (specs.arch == MemSpecs::ar_x86_64)
            writer.writeAttribute(xml::architecture, xml::arAMD64);
        else
            typeRuleWriterError(QString("Unknown architecture: %1").arg(specs.arch));
        writer.writeAttribute(xml::minver, specs.version.release);
        writer.writeAttribute(xml::maxver, specs.version.release);

        // Rule includes
        writer.writeStartElement(xml::ruleincludes); // ruleincludes
        writer.writeTextElement(xml::ruleinclude, "./" + name);
        writer.writeEndElement(); // ruleincludes

        QString fileName;

        // Go through all variables and write the rules
        for (int i = 0; !interrupted() && i < _factory->vars().size(); ++i) {
            ++_symbolsDone;
            checkOperationProgress();

            const Variable* var = _factory->vars().at(i);
            fileName = write(var, rulesDir);
            if (!fileName.isEmpty()) {
                writer.writeTextElement(xml::include, fileName);
                _filesWritten.append(QDir::cleanPath(rulesDir.absoluteFilePath(fileName)));
            }
        }

        // Go through all types and write the rules
        for (int i = 0; !interrupted() && i < _factory->types().size(); ++i) {
            ++_symbolsDone;
            checkOperationProgress();

            const BaseType* type = _factory->types().at(i);
            if (type->type() & StructOrUnion) {
                const Structured* s = dynamic_cast<const Structured*>(type);
                fileName = write(s, rulesDir);
                if (!fileName.isEmpty()) {
                    writer.writeTextElement(xml::include, fileName);
                    _filesWritten.append(QDir::cleanPath(rulesDir.absoluteFilePath(fileName)));
                }
            }
        }

        writer.writeEndElement(); // typeknowledge
        writer.writeEndDocument();
    }
    catch (...) {
        shellEndl();
        throw;
    }

    operationStopped();
    operationProgress();
    shellEndl();

    return _filesWritten.size();
}


void AltRefTypeRuleWriter::operationProgress()
{
    QString s = QString("\rWriting rules (%0%), %1 elapsed, %2 files written")
            .arg((int)((_symbolsDone / (float) _totalSymbols) * 100.0))
            .arg(elapsedTime())
            .arg(_filesWritten.size());
    shellOut(s, false);
}


QString AltRefTypeRuleWriter::uniqueFileName(const QDir &dir,
                                             const QString& fileName) const
{
    QFileInfo info(fileName);
    QString f(fileName);
    int i = 1;
    while (_filesWritten.contains(QDir::cleanPath(dir.absoluteFilePath(f)))) {
        f = QDir::cleanPath(info.path() + "/" + info.baseName() +
                            QString::number(i++) + "." + info.completeSuffix());
    }
    return f;
}


QString AltRefTypeRuleWriter::write(const Structured *s, const QDir &rulesDir) const
{
    // Skip custom structs (id < 0) that are specific to a global variable and
    // are not used by any type
    if (s->id() < 0 && _factory->typesUsingId(s->id()).isEmpty())
        return QString();

    QString fileName = rulesDir.relativeFilePath(
                uniqueFileName(rulesDir, fileNameFromType(s)));
    QString comment = QString("Type: %1\n" "Type ID: 0x%2\n")
                        .arg(s->prettyName())
                        .arg((uint)s->id(), 0, 16);
    QFile outFile(rulesDir.absoluteFilePath(fileName));
    QXmlStreamWriter writer;

    // Does the struct type have candidate types, probably in nested members?
    int count = writeStructDeep(s, QString(), comment, outFile, writer);

    if (outFile.isOpen()) {
        // Remove the file if we did not write any rules for it
        if (count > 0) {
            writer.writeEndDocument();
            return fileName;
        }
        else
            outFile.remove();
    }
    return QString();
}


QString AltRefTypeRuleWriter::write(const Variable *var, const QDir &rulesDir) const
{
    QString fileName = rulesDir.relativeFilePath(
                uniqueFileName(rulesDir, fileNameFromVar(var)));
    QString comment = QString("Variable: %1\n" "Variable ID: 0x%2\n")
                        .arg(var->prettyName())
                        .arg((uint)var->id(), 0, 16);
    QFile outFile(rulesDir.absoluteFilePath(fileName));
    QXmlStreamWriter writer;

    int count = 0;

    // Does the variable itself have any alternative types?
    if (var->hasAltRefTypes()) {
        // Prepare the XML file
        openXmlRuleFile(outFile, writer, comment);
        count += write(writer, var->altRefTypes(), var->name(), var->refType(),
                       ConstMemberList());

    }

    // Does the variable's type have candidate types? But only write rules for
    // variable-specific types (i.e., id < 0).
    const BaseType* varType = var->refTypeDeep(BaseType::trLexicalAndPointers);
    if (varType && (varType->type() & (StructOrUnion|rtArray)) && varType->id() < 0)
        count += writeStructDeep(varType, var->name(), comment, outFile, writer);

    if (outFile.isOpen()) {
        // Remove the file if we did not write any rules for it
        if (count > 0) {
            writer.writeEndDocument();
            return fileName;
        }
        else
            outFile.remove();
    }
    return QString();
}


int AltRefTypeRuleWriter::writeStructDeep(const BaseType* srcType,
                                          const QString& varName,
                                          const QString& xmlComment,
                                          QFile& outFile,
                                          QXmlStreamWriter& writer) const
{
    ConstMemberList members;
    QStack<int> memberIndex;
    const Structured *s, *cur;
    cur = s = dynamic_cast<const Structured*>(
                srcType->dereferencedBaseType(BaseType::trLexicalPointersArrays));
    // If s is an anonymous struct/union, try to find a typedef defining it
    if (s && s->name().isEmpty()) {
        BaseTypeList list = _factory->typesUsingId(s->id(), rtTypedef);
        if (!list.isEmpty())
            srcType = list.first();
    }

    int i = 0, count = 0;
    while (cur && i < cur->members().size()) {
        const StructuredMember *m = cur->members().at(i);
        members.append(m);
        if (m->hasAltRefTypes()) {
            // Prepare the XML file, if not yet done
            if (!outFile.isOpen()) {
                openXmlRuleFile(outFile, writer, xmlComment);
            }
            // Write the rule
            count += write(writer, m->altRefTypes(), varName, srcType, members);
        }

        // Use members and memberIndex to recurse through nested structs
        const BaseType* mt = m->refTypeDeep(BaseType::trLexical);
        if (mt->type() & StructOrUnion) {
            memberIndex.push(i);
            cur = dynamic_cast<const Structured*>(mt);
            i = 0;
        }
        else {
            ++i;
            members.pop_back();

            while (i >= cur->members().size() && !members.isEmpty()) {
                i = memberIndex.pop() + 1;
                cur = members.last()->belongsTo();
                members.pop_back();
            }
        }

    }

    return count;
}


int AltRefTypeRuleWriter::write(QXmlStreamWriter &writer,
                                const AltRefTypeList& altRefTypes,
                                const QString& varName,
                                const BaseType* srcType,
                                const ConstMemberList& members) const
{
    ASTConstExpressionList tmpExp;

    QStringList memberNames;
    foreach (const StructuredMember *member, members)
        memberNames += member->name();

    int count = 0;
    const ExprResultTypes evalErr =
            ExprResultTypes(erParameter|erRuntime|erUndefined);

    try {
        foreach(const AltRefType& art, altRefTypes) {
            // We require a valid expression
            if (!art.expr())
                continue;

            // Find non-pointer source type
            const BaseType* srcTypeNonTypedef =
                    srcType->dereferencedBaseType(BaseType::trLexical);
            const BaseType* srcTypeNonPtr =
                    srcTypeNonTypedef->dereferencedBaseType(BaseType::trLexicalAndPointers);
            const BaseType* srcTypeNonArray =
                    (srcTypeNonPtr && srcTypeNonPtr->type() == rtArray) ?
                    srcTypeNonPtr->dereferencedBaseType(rtArray, 1) : 0;
            // Check if we can use the target name or if we need to use the ID
            int srcUseId = useTypeId(srcType);

            // Find the target base type
            const BaseType* target = _factory->findBaseTypeById(art.id());
            if (!target)
                typeRuleWriterError(QString("Cannot find base type with ID 0x%1.")
                                    .arg((uint)art.id(), 0, 16));
            // Check if we can use the target name or if we need to use the ID
            int targetUseId = useTypeId(target);

            // Flaten the expression tree of alternatives
            ASTConstExpressionList alternatives = art.expr()->expandAlternatives(tmpExp);

            foreach(const ASTExpression* expr, alternatives) {
                bool skip = false;
                // Skip all expressions that we cannot evaluate
                ExprResultTypes resType = expr->resultType();
                if (resType & evalErr) {
                    writeComment(writer,
                                 QString("Cannot evaluate expression '%0' for "
                                         "candidate %1.%2.")
                                         .arg(expr->toString())
                                         .arg(srcType->name())
                                         .arg(memberNames.join(".")));
                    continue;
                }

                // Find all variable expressions
                ASTConstExpressionList varExpList = expr->findExpressions(etVariable);
                const ASTVariableExpression* varExp = 0;
                for (int i = 0; !skip && i < varExpList.size(); ++i) {
                    const ASTVariableExpression* ve =
                            dynamic_cast<const ASTVariableExpression*>(varExpList[i]);
                    if (ve) {
                        const BaseType* veTypeNonPtr = ve->baseType()
                                ? ve->baseType()->dereferencedBaseType(
                                            BaseType::trLexicalAndPointers)
                                : 0;
                        const BaseType* veTypeNonArray =
                                (veTypeNonPtr && veTypeNonPtr->type() == rtArray)
                                    ? veTypeNonPtr->dereferencedBaseType(rtArray, 1)
                                    : 0;
                        // If we don't have a base type or the variable's type
                        // does not match the given source type, we don't need
                        // to proceed
                        if ( !veTypeNonPtr ||
                             ( (*srcTypeNonPtr != *veTypeNonPtr) &&
                               // For array types, we ignore the array length by
                               // comparing the arrays' ref. type directly
                               (!srcTypeNonArray || !veTypeNonArray ||
                                *srcTypeNonArray != *veTypeNonArray) ) )
                        {
                            writeComment(writer,
                                         QString(" Source type in expression "
                                                 "'%1' does not match base type "
                                                 "'%2' of candidate %3.%4. ")
                                                 .arg(expr->toString())
                                                 .arg(srcType->prettyName())
                                                 .arg(srcType->name())
                                                 .arg(memberNames.join(".")));
                            skip = true;
                            break;
                        }
                        // Use the first one as the triggering variable expression
                        if (!varExp)
                            varExp = ve;
                        else {
                            // We expect exactly one type of variable expression
                            if (varExp->baseType()->id() != ve->baseType()->id()) {
                                typeRuleWriterError(QString("Expression %1 for %2.%3"
                                                            "has %4 different variables.")
                                                    .arg(expr->toString())
                                                    .arg(srcType->name())
                                                    .arg(memberNames.join("."))
                                                    .arg(varExpList.size()));
                                skip = true;
                                break;
                            }
                        }
                    }
                }

                // Make sure we found a valid variable expression
                if (!varExp || skip)
                    continue;

                QString skipReason;
                const BaseType* type = srcTypeNonTypedef;
                // Look for unsupported transformations in the form of
                // 'member,dereference,member', e.g. 's->foo->bar'
                bool m_found = false, m_deref_found = false;
                int m_idx = 0, nonTrivCnt = 0;
                foreach (const SymbolTransformation& trans,
                         varExp->transformations())
                {
                    if (trans.type == ttMember) {
                        ++nonTrivCnt;
                        if (!m_found) {
                            m_found = true;
                            // For the first member, skip any pointer if required
                            if (!(type->type() & StructOrUnion))
                                type = type->dereferencedBaseType(BaseType::trLexicalAndPointers);
                        }
                        // Skip transformations like 's->foo->bar'
                        else if (m_deref_found) {
                            skip = true;
                            skipReason =
                                    QString("Expression %1 contains a member "
                                            "access, dereference, and again "
                                            "member access in %2.%3.")
                                                .arg(expr->toString())
                                                .arg(srcType->name())
                                                .arg(memberNames.join("."));
                            break;
                        }

                        // Skip all anonymous members
                        while (m_idx < memberNames.size() &&
                               !trans.member.isEmpty() &&
                               memberNames[m_idx].isEmpty())
                            ++m_idx;

                        // We expect that the members in the array match the ones
                        // within the transformations
                        if (m_idx < memberNames.size() &&
                            trans.member != memberNames[m_idx])
                        {
                            skip = true;
                            skipReason =
                                    QString("Member names in expression %1 do "
                                            "not match members in %2.%3.")
                                                .arg(expr->toString())
                                                .arg(srcType->name())
                                                .arg(memberNames.join("."));
                            break;
                        }
                        ++m_idx;

                        // Does the source type has this member?
                        const Structured* s =
                                dynamic_cast<const Structured*>(type);
                        if (!s || !s->memberExists(trans.member, true)) {
                            skip = true;
                            skipReason =
                                    QString("Type '%1' has no member named '%2'"
                                            " in expression %3.")
                                            .arg(type->prettyName())
                                            .arg(trans.member)
                                            .arg(expr->toString());
                            break;
                        }
                        else
                            type = s->member(trans.member, true)
                                    ->refTypeDeep(BaseType::trLexical);
                    }
                    else if (trans.type & (ttDereference|ttArray)) {
                        if (trans.arrayIndex > 0)
                            ++nonTrivCnt;
                        if (m_found)
                            m_deref_found = true;
                        // Type must be a pointer or array
                        if (!(type->type() & (rtPointer|rtArray))) {
                            // Be forgiving before the first member access
                            if (m_found) {
                                skip = true;
                                skipReason =
                                        QString("Type '%1' is no pointer or "
                                                "array in expression %2.")
                                        .arg(type->prettyName())
                                        .arg(expr->toString());
                                break;
                            }
                        }
                        else
                            type = type->dereferencedBaseType(
                                        BaseType::trLexicalPointersArrays, 1);
                    }
                    else if (trans.type == ttFuncCall) {
                        ++nonTrivCnt;
                        type = type->dereferencedBaseType(
                                    BaseType::trLexicalPointersArrays, 1);
                        if (!(type->type() & (FunctionTypes))) {
                            skip = true;
                            skipReason =
                                    QString("Type '%1' is not a function or "
                                            "function pointer in expression %2.")
                                                .arg(type->prettyName())
                                                .arg(expr->toString());
                            break;
                        }
                        else
                            type = type->dereferencedBaseType(
                                        BaseType::trLexical|FunctionTypes);
                    }
                }

                // Is this a trivial expression that just returns itself?
                if (nonTrivCnt == 0) {
                    ASTConstExpressionList list = expr->flaten();
                    const BaseType* targetNonPtr = target->dereferencedBaseType(BaseType::trLexicalAndPointers);
                    if (list.size() == 1 && (srcTypeNonPtr == targetNonPtr ||
                                             *srcTypeNonPtr == *targetNonPtr))
                    {
                        skip = true;
                        skipReason =
                                QString("Expression %0 => '%1' is trivial and just "
                                        "returns the variable itself.")
                                            .arg(expr->toString())
                                            .arg(target->prettyName());
                    }
                }

                if (skip) {
                    writeComment(writer, skipReason);
                    continue;
                }

                // Calculate rules priority: each non-trivial transformation
                // increases the prio, vars have a base of 10
                int prio = nonTrivCnt;
                if (!varName.isEmpty())
                    prio += 10;

                QString exprStr = expr->toString(true);
                exprStr.replace("(" + varExp->baseType()->prettyName() + ")", _srcVar);

                QString ruleName(varName.isEmpty() ? srcType->name() : varName);
                if (!memberNames.isEmpty()) {
                    ruleName += "." + memberNames.join(".");
                    ruleName.replace(QRegExp("\\.\\.+"), ".");
                }

                writer.writeStartElement(xml::rule); // rule
                writer.writeAttribute(xml::priority, QString::number(prio));

                writer.writeTextElement(xml::name, ruleName);
                writer.writeTextElement(xml::description, expr->toString() +
                                        " => (" + target->prettyName() + ")");

                writer.writeStartElement(xml::filter); // filter
                if (!varName.isEmpty())
                    writer.writeTextElement(xml::variablename, varName);
                writer.writeTextElement(xml::datatype,
                                        realTypeToStr(srcTypeNonTypedef->type()).toLower());
                // Use the type name, if we can, otherwise the type ID
                if (!srcUseId) {
                    // Only write type name for types that actually have names
                    if (srcTypeNonTypedef->type() & (rtEnum|rtStruct|rtUnion|rtTypedef))
                        writer.writeTextElement(xml::type_name,
                                                srcTypeNonTypedef->name());
                }
                else {
                    writeComment(writer, QString("Source type '%1' is ambiguous")
                                                 .arg(srcTypeNonTypedef->prettyName()));
                    writer.writeTextElement(xml::type_id,
                                            QString("0x%0").arg((uint)srcUseId, 0, 16));
                }

                if (varExp->transformations().memberCount() > 0) {
                    writer.writeStartElement(xml::members); // members
                    // Add a filter rule for each member
                    int i = 0;
                    foreach (const SymbolTransformation& trans,
                             varExp->transformations())
                    {
                        if (trans.type == ttMember) {
                            while (i < memberNames.size() &&
                                   memberNames[i].isEmpty())
                            {
                                writer.writeTextElement(xml::member, QString());
                                ++i;
                            }
                            assert(i >= memberNames.size() ||
                                   memberNames[i] == trans.member);
                            ++i;
                            writer.writeTextElement(xml::member, trans.member);
                        }
                    }
                    writer.writeEndElement(); // members
                }
                writer.writeEndElement(); // filter

                writer.writeStartElement(xml::action); // action
                writer.writeAttribute(xml::type, xml::expression);
                // Use the source type name, if it is unique
                if (!srcUseId)
                    writer.writeTextElement(xml::srcType, varExp->baseType()->prettyName(_srcVar));
                else {
                    writeComment(writer, QString(" Source type '%1' is ambiguous ")
                                            .arg(srcType->prettyName()));
                    writer.writeTextElement(xml::srcType,
                                            QString("0x%0 %1")
                                                .arg((uint)srcUseId, 0, 16)
                                                .arg(_srcVar));
                }
                // Use the target type name, if it is unique
                if (!targetUseId)
                    writer.writeTextElement(xml::targetType, target->prettyName());
                else {
                    writeComment(writer, QString(" Target type '%1' is ambiguous ").arg(target->prettyName()));
                    writer.writeTextElement(xml::targetType,
                                            QString("0x%0").arg((uint)targetUseId, 0, 16));
                }

                writer.writeTextElement(xml::expression, exprStr);
                writer.writeEndElement(); // action

                writer.writeEndElement(); // rule

                ++count;
            }
        }
    }
    catch (...) {
        // exceptional cleanup
        foreach(const ASTExpression* expr, tmpExp)
            delete expr;
        throw;
    }

    // regular cleanup
    foreach(const ASTExpression* expr, tmpExp)
        delete expr;

    return count;
}


void AltRefTypeRuleWriter::writeComment(QXmlStreamWriter &writer,
                                        QString comment) const
{
    if (!comment.isEmpty()) {
        // avoid "--" in comments
        comment.replace("--", "- - ");
        if (comment.startsWith(' ') || comment.startsWith('\n'))
            writer.writeComment(comment);
        else
            writer.writeComment(" " + comment + " ");
    }

}


void AltRefTypeRuleWriter::openXmlRuleFile(QFile& outFile,
                                           QXmlStreamWriter& writer,
                                           const QString& comment) const
{
    if (_filesWritten.contains(QDir::cleanPath(outFile.fileName())))
        debugerr("Overwriting file " << outFile.fileName());
    if (!outFile.open(QIODevice::WriteOnly|QIODevice::Truncate))
        ioError(QString("Error opening file \"%1\" for writing.")
                    .arg(outFile.fileName()));

    writer.setDevice(&outFile);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(_indentation);

    writer.writeStartDocument();
    // Begin document with a comment
    writeComment(writer, QString("\nFile created on %1\n%2")
                            .arg(QDateTime::currentDateTime().toString())
                            .arg(comment));

    writer.writeStartElement(xml::typeknowledge); // typeknowledge
    writer.writeAttribute(xml::version, QString::number(xml::currentVer));
    writer.writeStartElement(xml::rules); // rules
}


QString AltRefTypeRuleWriter::fileNameFromType(const BaseType *type) const
{
    QString s = QString("type_%1.xml").arg(fileNameEscape(type->prettyName()));
    if (type->name().isEmpty())
        s = s.replace(str::anonymous,
                      QString("0x%1").arg((uint)type->id(), 0, 16));
    return s;
}


QString AltRefTypeRuleWriter::fileNameFromVar(const Variable *var) const
{
    QString n(var->name());
    if (n.isEmpty())
        n = QString("0x%1").arg((uint)var->id(), 0, 16);
    QString varTypeName = var->refType() ?
                var->refType()->prettyName() : QString("void");
    QString s = QString("var_%1_%2.xml")
                    .arg(n)
                    .arg(fileNameEscape(varTypeName));
    if (var->refType() && var->refType()->name().isEmpty())
        s = s.replace(str::anonymous,
                      QString("0x%1").arg((uint)var->refTypeId(), 0, 16));
    return s;
}


QString AltRefTypeRuleWriter::fileNameEscape(QString s) const
{
    s = s.trimmed();
    s = s.replace(QRegExp("\\[([^\\]]*)\\]"), "_array\\1");
    s = s.replace(QChar('?'), "N");
    s = s.replace(QChar(' '), "_");
    s = s.replace(QChar('*'), "ptr");
    return s;
}


int AltRefTypeRuleWriter::useTypeId(const BaseType* type) const
{
    const BaseType* typeNonPtr = type ?
                type->dereferencedBaseType(BaseType::trLexicalAndPointers) : 0;

    int typeId = 0;

    if (typeNonPtr && (typeNonPtr->type() & StructOrUnion) &&
        typeNonPtr->name().isEmpty())
        typeId = type->dereferencedBaseType(BaseType::trLexical)->id();
    else if (!typeNonPtr || typeNonPtr->name().isEmpty() ||
             _factory->findBaseTypesByName(typeNonPtr->name()).size() > 1)
    {
        try {
            QString s;
            ExpressionAction action;
            const BaseType *t = action.parseTypeStr(
                        QString(), 0, _factory, QString(),
                        type->prettyName(), s);
            Q_UNUSED(t);
        }
        catch (TypeRuleException& e) {
            if ((e.errorCode == TypeRuleException::ecTypeAmbiguous) ||
                (e.errorCode == TypeRuleException::ecNotCompatible))
                typeId = type->dereferencedBaseType(BaseType::trLexical)->id();
            else
                throw;
        }
    }

    // If our type ID is less than 0, find the original type's ID
    if (typeId < 0) {
        BaseTypeUIntHash::const_iterator
                it = _factory->typesByHash().find(type->hash()),
                e = _factory->typesByHash().end();
        while (it != e && it.key() == type->hash()) {
            const BaseType* t = it.value();
            if (t->id() > 0 && *t == *type)
                return t->id();
            ++it;
        }
    }
    return typeId;
}
