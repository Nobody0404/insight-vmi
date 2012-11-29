#include "typeruleengine.h"
#include "typeruleexception.h"
#include "typerule.h"
#include "typerulereader.h"
#include "osfilter.h"
#include "symfactory.h"
#include "shell.h"
#include "shellutil.h"
#include "scriptengine.h"
#include "refbasetype.h"
#include "variable.h"
#include <debug.h>

namespace js
{
const char* arguments = "arguments";
const char* inlinefunc = "__inline_func__";
}


TypeRuleEngine::TypeRuleEngine()
    : _eng(new ScriptEngine (ksNone)), _verbose(veOff)
{
}


TypeRuleEngine::~TypeRuleEngine()
{
    clear();
    delete _eng;
}


void TypeRuleEngine::clear()
{
    for (int i = 0; i < _rules.size(); ++i)
        delete _rules[i];
    _rules.clear();
    _activeRules.clear();
    _rulesPerType.clear();

    for (OsFilterHash::const_iterator it = _osFilters.constBegin(),
         e = _osFilters.constEnd(); it != e; ++it)
    {
        delete it.value();
    }
    _osFilters.clear();
    _ruleFiles.clear();
    _hits.clear();
}


void TypeRuleEngine::readFrom(const QString &fileName, bool forceRead)
{
    readFrom(QStringList(fileName), forceRead);
}


void TypeRuleEngine::readFrom(const QStringList &fileNames, bool forceRead)
{
    TypeRuleReader reader(this, forceRead);
    try {
        reader.readFrom(fileNames);
    }
    catch (...) {
        // Remove this file from the list of read files
        int index = _ruleFiles.indexOf(reader.currFile());
        if (index >= 0)
            _ruleFiles[index] = QString();
        throw;
    }
}


void TypeRuleEngine::appendRule(TypeRule *rule, const OsFilter *osf)
{
    if (!rule)
        return;

    const OsFilter* filter = insertOsFilter(osf);
    rule->setOsFilter(filter);
    _rules.append(rule);
}


int TypeRuleEngine::indexForRuleFile(const QString &fileName)
{
    int index = _ruleFiles.indexOf(fileName);

    if (index <= 0) {
        index = _ruleFiles.size();
        _ruleFiles.append(fileName);
    }
    return index;
}


bool TypeRuleEngine::fileAlreadyRead(const QString &fileName)
{
    return _ruleFiles.contains(fileName);
}


int TypeRuleEngine::addAllLexicalTypes(const BaseType* type, ActiveRule& arule)
{
    if (!type)
        return 0;

    int hits = 0;
    // If the rule matches the original type, we add it for all lexical types
    // as well
    if (arule.rule->match(type) && arule.rule->action()->match(type)) {
        do {
            _rulesPerType.insertMulti(type->id(), arule);
            ++hits;
            if (type->type() & BaseType::trLexical)
                type = dynamic_cast<const RefBaseType*>(type)->refType();
            else
                break;
        } while (type);
    }
    return hits;
}


void TypeRuleEngine::checkRules(SymFactory *factory, const OsSpecs* specs, int from)
{
    operationStarted();
    _rulesChecked = 0;

    // Full or partial check?
    if (from <= 0) {
        from = 0;
        _hits.fill(0, _rules.size());
        _activeRules.clear();
        _rulesPerType.clear();
        _rulesToCheck = _rules.size();
    }
    else {
        _hits.resize(_rules.size());
        _rulesToCheck = _rules.size() - from;
    }

    if (!factory->symbolsAvailable() || _rules.isEmpty())
        return;

    forceOperationProgress();

    // Checking the rules from last to first assures that rules in the
    // _activeRules hash are processes first to last. That way, if multiple
    // rules match the same instance, the first rule takes precedence.
    for (int i = _rules.size() - 1; !interrupted() && i >= from; --i) {
        ++_rulesChecked;
        checkOperationProgress();

        TypeRule* rule = _rules[i];

        // Check for OS filters first
        if (rule->osFilter() && !rule->osFilter()->match(specs))
            continue;

        // Make sure a filter is specified
        if (!rule->filter()) {
            warnRule(rule, i, "is ignored because it does not specify a filter.");
            continue;
        }

        try {
            rule->action()->check(ruleFile(rule), rule, factory);
        }
        catch (TypeRuleException& e) {
            QString s = QString("raised a %0:\n").arg(e.className());
            if (e.xmlLine > 0) {
                s += QString("At line %0").arg(e.xmlLine);
                if (e.xmlColumn > 0) {
                    s += QString(" column %0").arg(e.xmlColumn);
                }
                s += ": ";
            }
            s += e.message;
            errRule(rule, i, s);
            continue;
        }
        catch (GenericException& e) {
            errRule(rule, i, QString("raised a %1 at %2:%3: %4")
                    .arg(e.className())
                    .arg(e.file)
                    .arg(e.line)
                    .arg(e.message));
//            throw;
            continue;
        }

        QScriptProgramPtr prog;

        ActiveRule arule(i, rule, prog);

        // Should we check variables or types?
        int hits = 0;
        // If we have a type name or a type ID, we don't have to try all rules
        if (rule->filter()->filterActive(Filter::ftTypeNameLiteral)) {
            const QString& name = rule->filter()->typeName();
            BaseTypeStringHash::const_iterator
                    it = factory->typesByName().find(name),
                    e = factory->typesByName().end();
            while (it != e && it.key() == name) {
                hits += addAllLexicalTypes(it.value(), arule);
                ++it;
            }
        }
        else if (rule->filter()->filterActive(Filter::ftTypeId)) {
            const BaseType* t = factory->findBaseTypeById(rule->filter()->typeId());
            hits += addAllLexicalTypes(t, arule);
        }
        else {
            foreach (const BaseType* bt, factory->types()) {
                hits += addAllLexicalTypes(bt, arule);
            }
        }

        // Warn if a rule does not match
        if (hits) {
            _hits[i] = hits;
            _activeRules.append(arule);
        }
        else
            warnRule(rule, i, "does not match any type.");
    }

    operationStopped();
    operationProgress();
    shellEndl();
}


QString TypeRuleEngine::ruleFile(const TypeRule *rule) const
{
    // Check the bounds first
    if (rule && rule->srcFileIndex() >= 0 &&
        rule->srcFileIndex() < _ruleFiles.size())
        return _ruleFiles[rule->srcFileIndex()];
    return QString();
}


int TypeRuleEngine::match(const Instance *inst, const ConstMemberList &members,
                          Instance** newInst, int *priority) const
{
    if (priority)
        *priority = 0;

    if (!inst || !inst->type())
        return mrNoMatch;

    int ret = mrNoMatch, prio = 0, usedRule = -1, tmp_ret = 0;
    *newInst = 0;
    int ruleInfosPrinted = 0;

    ActiveRuleHash::const_iterator it = _rulesPerType.find(inst->type()->id()),
            e = _rulesPerType.end();

    // Rules with variable names might need to match inst->name(), so check all.
    // We can stop as soon as all possibly ORed values are included.
    for (; it != e && it.key() == inst->type()->id(); ++it) {
        const TypeRule* rule = it.value().rule;
        int index = it.value().index;

        // If the result is already clear, check only for higher priority rules
        if (ret == (mrMatch|mrAmbiguous|mrDefer) && rule->priority() <= prio) {
            // Output information
            if (_verbose)
                if (ruleMatchInfo(it.value(), inst, members, 0, false, true))
                    ++ruleInfosPrinted;
            continue;
        }

        const InstanceFilter* filter = rule->filter();
        bool match = false, evaluated = false, skipped = true;
        tmp_ret = 0;

        // Ignore rules with a lower priority than the previously matched one
        if (filter && (!(ret & mrMatch) || rule->priority() >= prio)) {
            skipped = false;
            if (!filter->filterActive(Filter::ftVarNameAll) ||
                filter->matchInst(inst))
            {
                // Are all required fields accessed?
                // Not all fields given ==> defer
                if (filter->members().size() > members.size()) {
                    ret |= tmp_ret |= mrDefer;
                }
                // Required no. of fields given ==> match
                // However, we don't need to evaluate anymore non-high-prio
                // rules if both mrMatch and mrAmbiguous are already set
                else if (filter->members().size() == members.size() &&
                         (!((ret & mrMatch) && (ret & mrAmbiguous)) ||
                          rule->priority() > prio))
                {
                    match = true;
                    for (int i = 0; match && i < members.size(); ++i)
                        if (!filter->members().at(i).match(members[i]))
                            match = false;

                    if (match) {
                        // Evaluate the rule
                        bool alreadyMatched = (ret & mrMatch);
                        evaluated = true;
                        Instance instRet(evaluateRule(it.value(), inst, members,
                                                      &match));
                        instRet.setOrigin(Instance::orRuleEngine);
                        ret |= tmp_ret |= mrMatch;

                        // If this rule has a higher priority, it overrides
                        // the previous
                        if (rule->priority() > prio) {
                            tmp_ret &= ret &= ~(mrAmbiguous|mrDefaultHandler);
                            alreadyMatched = false;
                        }
                        // Did another rule match previously?
                        else if ( (*newInst) && alreadyMatched ) {
                            // Compare this instance to the previous one
                            if (instRet.address() != (*newInst)->address() ||
                                (instRet.typeHash() != (*newInst)->typeHash()))
                            {
                                // Ambiguous rules
                                ret |= tmp_ret |= mrAmbiguous;
                                usedRule = -1;
                            }
                        }

                        // Is this the first match for the current priority?
                        if (!alreadyMatched) {
                            prio = rule->priority();
                            usedRule = index;
                            if (priority)
                                *priority = prio;
                            // The rule returned an instance (valid or invalid)
                            if (match) {
                                // Save given instance, if valid
                                if ( (*newInst) )
                                    **newInst = instRet;
                                else if (instRet.isValid())
                                    *newInst = new Instance(instRet);
                            }
                            // The rule asked to fall back to the default handler
                            else
                                ret |= tmp_ret |= mrDefaultHandler;
                        }
                    }
                }
            }
        }
        // Output information
        if (_verbose) {
            if (ruleMatchInfo(it.value(), inst, members, tmp_ret, evaluated, skipped))
                ++ruleInfosPrinted;
        }
    }

    if (ruleInfosPrinted > 0) {
        shell->out() << "==> Result: ";
        if (usedRule >= 0)
            shell->out() << "applied rule " << shell->color(ctBold)
                         << (usedRule + 1) << shell->color(ctReset) << " ";

        if (ret & mrMatch)
            shell->out() << "prio. " << prio << " "
                         << shell->color(ctMatched) << "matched ";
        if (ret & mrDefaultHandler)
            shell->out() << shell->color(ctEvaluated) << "ret.orig. ";
        if (ret & mrDefer)
            shell->out() << shell->color(ctDeferred) << "deferred ";
        if (ret & mrAmbiguous)
            shell->out() << shell->color(ctMissed) << "ambiguous ";
        if (!ret)
            shell->out() << shell->color(ctMissed) << "missed ";

        shell->out() << shell->color(ctReset) << endl;
    }

    // Don't return invalid instances
    if ((*newInst) && (!(*newInst)->isValid() || (ret & mrDefaultHandler))) {
        delete *newInst;
        *newInst = 0;
    }

    return ret;
}

template<class T>
ActiveRuleList rulesMatchingTempl(const T *t, const ConstMemberList &members,
                                  const ActiveRuleHash& rules,
                                  const BaseType* (*get_type_func)(const T*))
{
    ActiveRuleList ret;
    const BaseType* srcType;

    if (!t || !(srcType = get_type_func(t)) )
        return ret;

    int prio = 0;
    ActiveRuleHash::const_iterator it = rules.find(srcType->id()),
            e = rules.end();

    // Check out all matching rules
    for (; it != e && it.key() == srcType->id(); ++it) {
        const TypeRule* rule = it.value().rule;
        const InstanceFilter* filter = rule->filter();

        // Ignore rules with a lower priority than the previously matched one
        if (rule->priority() >= prio && filter &&
            filter->members().size() == members.size() &&
            rule->match(t))
        {
            bool match = true;
            for (int i = 0; match && i < members.size(); ++i)
                if (!filter->members().at(i).match(members[i]))
                    match = false;
             if (!match)
                    continue;
            // If this rule has higher priority, remove all other rules
            if (rule->priority() > prio)
                ret.clear();
            if (ret.isEmpty())
                prio = rule->priority();
            // Append this rule to the list
            ret += it.value();
        }
    }

    return ret;
}


inline const BaseType* get_inst_type(const Instance* inst)
{
    return inst->type();
}


ActiveRuleList TypeRuleEngine::rulesMatching(const Instance *inst,
                                             const ConstMemberList &members) const
{
    return rulesMatchingTempl(inst, members, _rulesPerType, &get_inst_type);
}


inline const BaseType* get_var_type(const Variable* var)
{
    return var->refType();
}


ActiveRuleList TypeRuleEngine::rulesMatching(const Variable *var,
                                             const ConstMemberList &members) const
{
    return rulesMatchingTempl(var, members, _rulesPerType, &get_var_type);
}


inline const BaseType* get_bt_type(const BaseType* type)
{
    return type;
}


ActiveRuleList TypeRuleEngine::rulesMatching(const BaseType *type,
                                             const ConstMemberList &members) const
{
    return rulesMatchingTempl(type, members, _rulesPerType, &get_bt_type);
}


Instance TypeRuleEngine::evaluateRule(const ActiveRule& arule,
                                      const Instance *inst,
                                      const ConstMemberList &members,
                                      bool* matched) const
{
    bool m = false;
    Instance ret;
    if (arule.rule->action()) {
        try {
            ret = arule.rule->action()->evaluate(inst, members, _eng, &m);
            if (matched)
                *matched = m;

            // Check for pointers that point back into or overlap with "inst"
            if (inst->overlaps(ret) && !members.isEmpty()) {
                // This is most likely an empty list-like structure, so return the
                // member where the address points to
                const BaseType* rt;
                quint64 realAddr = inst->address();
                ret = *inst;
                Instance tmp;
                // Find out where the pointer points to natively (before rules)
                for (int i = 0; i < members.size(); ++i) {
                    tmp = ret.member(members[i]->index(), 0, 0, ksNone);
                    if (tmp.isValid())
                        ret = tmp.dereference(BaseType::trLexical);
                    else
                        break;
                }
                // Is the last member a pointer or numeric type?
                if (ret.type()->type() & (rtPointer|rtInt32|rtInt64|rtUInt32|rtUInt64))
                    realAddr = (quint64)ret.toPointer();

                // Now find the member of inst where the real address points to
                ret = *inst;
                int i = 0;
                // Resolve the members as long as they are structs/unions
                while (i < members.size() &&
                       ret.address() + members[i]->offset() >= realAddr &&
                       (rt = members[i]->refTypeDeep(BaseType::trLexical)) &&
                       (rt->type() & StructOrUnion))
                {
                    ret = ret.member(members[i]->index(), 0, 0, ksNone);
                    ++i;
                }
            }
        }
        catch (VirtualMemoryException& /*e*/) {
            // ignored
            return Instance(Instance::orRuleEngine);
        }
        catch (GenericException& e) {
//            errRule(arule.rule, arule.index, QString("raised an exception."));
            errRule(arule.rule, arule.index, QString("raised a %1 at %2:%3: %4")
                    .arg(e.className())
                    .arg(e.file)
                    .arg(e.line)
                    .arg(e.message));
            return Instance(Instance::orRuleEngine);
        }
    }
    ret.setOrigin(Instance::orRuleEngine);
    return ret;
}


bool TypeRuleEngine::ruleMatchInfo(const ActiveRule& arule,
                                   const Instance *inst,
                                   const ConstMemberList &members,
                                   int matched, bool evaluated, bool skipped) const
{
    // Verbose output
    if ((_verbose >= veAllRules) ||
        (_verbose >= veMatchingRules && (matched & mrMatch)) ||
        (_verbose >= veDeferringRules && matched) ||
        (_verbose >= veEvaluatedRules && evaluated))
    {
        int w = ShellUtil::getFieldWidth(_rules.size(), 10);
        shell->out() << "Rule " << shell->color(ctBold)
                     << qSetFieldWidth(w) << right << (arule.index + 1)
                     << qSetFieldWidth(0) << shell->color(ctReset)
                     << " prio."
                     << qSetFieldWidth(4) << right << arule.rule->priority()
                     << qSetFieldWidth(0) << left << " ";
        if (skipped)
            shell->out() << shell->color(ctDim) << "skipped";
        else {
            if (matched & mrMatch) {
                shell->out() << shell->color(ctMatched) << "matches";
                if ((matched & mrDefer))
                    shell->out() << " and " << shell->color(ctDeferred) << "defers";
            }
            else if ((matched & mrDefer))
                shell->out() << shell->color(ctDeferred) << "defers ";
            else
                shell->out() << shell->color(ctMissed) << "misses ";

            shell->out() << shell->color(ctReset) << " instance ";
            if (inst)
                shell->out() << shell->color(ctBold)
                             << ShellUtil::abbrvStrLeft(inst->fullName(), 60)
                             << shell->color(ctReset);
            for (int i = 0; i < members.size(); ++i) {
                if (i > 0 || inst)
                    shell->out() << ".";
                shell->out() << shell->color(ctMember) << members[i]->name()
                             << shell->color(ctReset);
            }

            if (matched & mrMatch) {
                if (evaluated)
                    shell->out() << " and is " << shell->color(ctEvaluated)
                                 << "evaluated";
                else
                    shell->out() << " but is " << shell->color(ctDeferred)
                                 << "ignored";
            }
        }

        shell->out() << shell->color(ctReset) << endl;
        return true;
    }

    return false;
}


void TypeRuleEngine::warnRule(const TypeRule* rule, int index, const QString &msg) const
{
    ruleMsg(rule, index, "Warning", msg, ctWarningLight, ctWarning);
}


void TypeRuleEngine::errRule(const TypeRule* rule, int index, const QString &msg) const
{
    ruleMsg(rule, index, "Error", msg, ctErrorLight, ctError);
}


void TypeRuleEngine::ruleMsg(const TypeRule* rule, int index,
                             const QString &severity, const QString &msg,
                             ColorType light, ColorType normal) const
{
    if (!rule)
        return;

    shellEndl();
    shell->err() << shell->color(light) << severity << ": "
                 << shell->color(normal) << "Rule "
                 << shell->color(ctBold) << (index + 1)
                 << shell->color(normal) << " ";

    if (!rule->name().isEmpty()) {
        shell->err() << "(" << shell->color(ctBold) << rule->name()
                     << shell->color(normal) << ") ";
    }
    if (rule->srcFileIndex() >= 0) {
        // Use as-short-as-possible file name
        QString file(ShellUtil::shortFileName(ruleFile(rule)));

        shell->err() << "defined in "
                     << shell->color(ctBold) << file << shell->color(normal)
                     << " line "
                     << shell->color(ctBold) << rule->srcLine()
                     << shell->color(normal) << " ";
    }

    shell->err() << msg << shell->color((ctReset)) << endl;
}


const OsFilter *TypeRuleEngine::insertOsFilter(const OsFilter *osf)
{
    if (!osf)
        return 0;

    uint hash = qHash(*osf);
    const OsFilter* filter = 0;
    // Do we already have this filter in our list?
    OsFilterHash::const_iterator it = _osFilters.find(hash);
    while (it != _osFilters.end() && it.key() == hash)
        if (*it.value() == *osf)
            return it.value();

    // This is a new filter, so create a copy and insert it into the list
    filter = new OsFilter(*osf);
    _osFilters.insertMulti(hash, filter);

    return filter;
}


void TypeRuleEngine::operationProgress()
{
    QString s = QString("\rVerifying rules (%0%), %1 elapsed, %2 rules active")
            .arg(_rulesToCheck ? (int)((_rulesChecked / (float) _rulesToCheck) * 100.0) : 100)
            .arg(elapsedTime())
            .arg(_activeRules.size());
    shellOut(s, false);
}
