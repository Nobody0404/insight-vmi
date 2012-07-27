/*
 * memorymapbuildersv.cpp
 *
 *  Created on: 13.10.2010
 *      Author: chrschn
 */

#include "memorymapbuildersv.h"

#include <QMutexLocker>

#include "memorymap.h"
#include "virtualmemory.h"
#include "virtualmemoryexception.h"
#include "array.h"
#include "memorymapverifier.h"
#include <debug.h>


MemoryMapBuilderSV::MemoryMapBuilderSV(MemoryMap* map, int index)
    : MemoryMapBuilder(map, index)
{
}


MemoryMapBuilderSV::~MemoryMapBuilderSV()
{
}


void MemoryMapBuilderSV::run()
{
    _interrupted = false;
    // Holds the data that is shared among all threads
    BuilderSharedState* shared = _map->_shared;

    MemoryMapNode* node = 0;

    // Now work through the whole stack
    QMutexLocker queueLock(&shared->queueLock);
    while ( !_interrupted && !shared->queue.isEmpty() &&
            (!shared->lastNode ||
             shared->lastNode->probability() >= shared->minProbability) &&
            _map->verifier().lastVerification())
    {
        // Take element with highest probability
        node = shared->queue.takeLargest();
        shared->lastNode = node;
        ++shared->processed;
        queueLock.unlock();

#if MEMORY_MAP_VERIFICATION == 1
        _map->verifier().newNode(node);
#endif

        // Verify address
        /*
        if(!verifier.verifyAddress(node->address())) {
            // The node is not valid
            // Does it have a valid candidate?
            NodeList cand = node->getCandidates();
            int i = 0;

            for(i = 0; i < cand.size(); ++i) {
                if(verifier.verifyAddress(cand.at(i)->address()))
                    break;
            }

            if(i == cand.size())
                if(node->parent()) {
                    debugmsg("Adr (" << node->address() << ") of node "
                             << node->fullName() << " ("
                            << node->type()->prettyName() << ", "
                             << node->parent()->type()->prettyName() << ") invalid!");
                }
                else {
                    debugmsg("Adr (" << node->address() << ") of node "
                             << node->fullName() << " ("
                            << node->type()->prettyName() << ") invalid!");
                }
        }
        */

        // Insert in non-critical (non-exception prone) mappings
        shared->typeInstancesLock.lock();
        _map->_typeInstances.insert(node->type()->id(), node);
        if (shared->maxObjSize < node->size())
            shared->maxObjSize = node->size();
        shared->typeInstancesLock.unlock();

        // try to save the physical mapping
        try {
            int pageSize;
            quint64 physAddr = _map->_vmem->virtualToPhysical(node->address(), &pageSize);
            // Linear memory region or paged memory?
            if (pageSize < 0) {
                PhysMemoryMapNode pNode(
                            physAddr,
                            node->size() > 0 ? physAddr + node->size() - 1 : physAddr,
                            node);
                shared->pmemMapLock.lock();
                _map->_pmemMap.insert(pNode);
                shared->pmemMapLock.unlock();
            }
            else {
                // Add all pages a type belongs to
                quint32 size = node->size();
                quint64 virtAddr = node->address();
                quint64 pageMask = ~(pageSize - 1);

                while (size > 0) {
                    physAddr = _map->_vmem->virtualToPhysical(virtAddr, &pageSize);
                    // How much space is left on current page?
                    quint32 sizeOnPage = pageSize - (virtAddr & ~pageMask);
                    if (sizeOnPage > size)
                        sizeOnPage = size;
                    PhysMemoryMapNode pNode(
                                physAddr, physAddr + sizeOnPage - 1, node);

                    // Add a memory mapping
                    shared->pmemMapLock.lock();
                    _map->_pmemMap.insert(pNode);
                    shared->pmemMapLock.unlock();
                    // Subtract the available space from left-over size
                    size -= sizeOnPage;
                    // Advance address
                    virtAddr += sizeOnPage;
                }
            }
        }
        catch (VirtualMemoryException&) {
            // Lock the mutex again before we jump to the loop condition checking
            queueLock.relock();
            // Don't proceed any further in case of an exception
            continue;
        }

        // Create an instance from the node
        Instance inst = node->toInstance(false);

        // If this is a pointer, add where it's pointing to
        if (node->type()->type() & rtPointer) {
            try {
                quint64 addr = (quint64) inst.toPointer();
                shared->pointersToLock.lock();
                _map->_pointersTo.insert(addr, node);
                shared->pointersToLock.unlock();
                // Add dereferenced type to the stack, if not already visited
                int cnt = 0;
                inst = inst.dereference(BaseType::trLexicalAndPointers, -1, &cnt);
//                inst = inst.dereference(BaseType::trLexical, &cnt);

                // Check for NULL Pointer or Pointer that have a value of -1
                // Further all pointers must be 4 byte aligned
                if (cnt && _map->addressIsWellFormed(inst))
                    _map->addChildIfNotExistend(inst, node, _index, node->address());
            }
            catch (GenericException& e) {
                // Do nothing
            }
        }
        // If this is an array, add all elements
        else if (node->type()->type() & rtArray) {
            const Array* a = dynamic_cast<const Array*>(node->type());
            if (a->length() > 0) {
                // Add all elements to the stack that haven't been visited
                for (int i = 0; i < a->length(); ++i) {
                    try {
                        Instance e = inst.arrayElem(i);
                        if (_map->addressIsWellFormed(e))
                            _map->addChildIfNotExistend(e, node, _index, node->address() + (i * e.size()));
                    }
                    catch (GenericException& e) {
                        // Do nothing
                    }
                }
            }
        }
        // If this is a struct, add all pointer members
        else if (inst.memberCount() > 0) {
            addMembers(&inst, node);
        }

#if MEMORY_MAP_VERIFICATION == 1
        _map->verifier().performChecks(node);
#endif

        // Lock the mutex again before we jump to the loop condition checking
        queueLock.relock();
    }
}

void MemoryMapBuilderSV::addMembers(const Instance *inst, MemoryMapNode* node)
{
    if (!inst->memberCount())
        return;

    // Possible pointer types: Pointer and integers of pointer size
    const int ptrTypes = rtPointer | rtFuncPointer |
            ((_map->vmem()->memSpecs().arch & MemSpecs::ar_i386) ?
                 (rtInt32 | rtUInt32) :
                 (rtInt64 | rtUInt64));

    // Add all struct members to the stack that haven't been visited
    for (int i = 0; i < inst->memberCount(); ++i) {
        // Get declared type of this member
        const BaseType* mBaseType = inst->memberType(i, BaseType::trLexical, true);
        if (!mBaseType)
            continue;

        // Consider the members of nested structs recurisvely
        // Ignore Unions for now
        // if (mBaseType->type() & StructOrUnion) {
        if (mBaseType->type() & rtStruct) {
            Instance mi = inst->member(i, BaseType::trLexical, -1, true);
            // Adjust instance name to reflect full path
            if (node->name() != inst->name())
                mi.setName(inst->name() + "." + mi.name());

            _map->addChildIfNotExistend(mi, node, _index, inst->memberAddress(i));

            // addMembers(&mi, node);
            continue;
        }

        // Skip members which cannot hold pointers
        const int candidateCnt = inst->memberCandidatesCount(i);
        if (!candidateCnt && !(mBaseType->type() & ptrTypes))
            continue;

        // Skip self and invalid pointers
        if(mBaseType->type() & rtPointer) {
             Instance m = inst->member(i, BaseType::trLexical, -1, true);

             if((quint64)m.toPointer() == (inst->address() + inst->memberAddress(i, true)) ||
                 (quint64)m.toPointer() == inst->address() ||
                 !_map->addressIsWellFormed((quint64)m.toPointer()))
                 continue;
        }
        // Only one candidate and no unions.
        if (candidateCnt <= 1 && !(node->type()->type() & rtUnion)) {
            try {
                Instance m = inst->member(i, BaseType::trLexical, -1, true);
                // Only add pointer members with valid address
                if (m.type() && m.type()->type() == rtPointer &&
                    _map->addressIsWellFormed(m))
                {
                    int cnt = 1;

                    if(candidateCnt == 1)
                        m = inst->member(i, BaseType::trLexicalAndPointers, -1);
                    else
                        m = m.dereference(BaseType::trLexicalPointersArrays, -1, &cnt);
                    if (cnt && !m.isNull()) {
                        // Adjust instance name to reflect full path
                        if (node->name() != inst->name())
                            m.setName(inst->name() + "." + m.name());
                        _map->addChildIfNotExistend(m, node, _index, inst->memberAddress(i));
                    }
                }
            }
            catch (GenericException& e) {
                // Do nothing
            }
        }
        // Multiple candidates, add all that make sense at first glance
        else {
#if MEMORY_MAP_PROCESS_NODES_WITH_ALT == 0
            continue;
        }
#elif MEMORY_MAP_PROCESS_NODES_WITH_ALT == 1

            MemoryMapNode *lastNode = NULL;
            float penalize = 0.0;

            // Lets first try to add the original type of the member
            // This approach makes sure we get the correct type even if
            // the candidates are incorrect due to weird hacks within the
            // kernel
            try {
                Instance m = inst->member(i, BaseType::trLexical, -1, true);
                // Only add pointer members with valid address
                if (m.type() && m.type()->type() == rtPointer &&
                    _map->addressIsWellFormed(m))
                {
                    int cnt;
                    m = m.dereference(BaseType::trLexicalPointersArrays, -1, &cnt);
                    if (cnt || (m.type()->type() & StructOrUnion)) {
                        // Adjust instance name to reflect full path
                        if (node->name() != inst->name())
                            m.setName(inst->name() + "." + m.name());

                       // Member has condidates
                       lastNode = _map->addChildIfNotExistend(m, node, _index, inst->memberAddress(i), true);
                    }
                }
            }
            catch (GenericException& e) {
                // Do nothing
            }

            for (int j = 0; j < candidateCnt; ++j) {
                // Reset the penalty
                penalize = 0.0;

                // Check for compatibility
                if(!inst->memberCandidateCompatible(i, j)) {
                    //debugmsg("cont");
                    continue;
                }

                try {
                    const BaseType* type = inst->memberCandidateType(i, j);
                    type = type ? type->dereferencedBaseType(BaseType::trLexical) : 0;

                    // Skip invalid and void* candidates
                    if (!type ||
                        ((type->type() & (rtPointer|rtArray)) &&
                         !dynamic_cast<const Pointer*>(type)->refType()))
                        continue;

                    // Try this candidate
                    Instance m = inst->memberCandidate(i, j);

                    if(_map->addressIsWellFormed(m)) {
                        // We handle list_head instances seperately.
                        if(inst->isListHead()) {
                            // The candidate type must be a structure
                            if(!(m.type()->type() & StructOrUnion)) {
                                //debugmsg("Out 1: " << m.fullName());

                                // Penalty of 90%
                                penalize = 0.9;
                            }
                            else {
                                // Get the instance of the 'next' member within the list_head
                                void *memberNext = inst->member(i, 0, -1, true).toPointer();

                                // If this list head points to itself, we do not need to consider it
                                // anymore.
                                if((quint64)memberNext == inst->member(i, 0, -1, true).address())
                                    continue;

                                // The pointer can be NULL
                                if((quint64)memberNext != 0) {
                                    // Get the offset of the list_head struct within the candidate type
                                    quint64 candOffset = (quint64)memberNext - m.address();

                                    // Find the member based on the calculated offset within the candidate type
                                    Instance candListHead = m.memberByOffset(candOffset);

                                    // The member within the candidate type that the next pointer points to
                                    // must be a list_head.
                                    if(!candListHead.isNull() && candListHead.isListHead())
                                    {
                                        // Sanity check: The prev pointer of the list_head must point back to the
                                        // original list_head
                                        void *candListHeadPrev = candListHead.member(1, 0, -1, true).toPointer();

                                        if((quint64)candListHeadPrev == (quint64)memberNext)
                                        {
                                            // At this point we know that the list_head struct within the candidate
                                            // points indeed back to the list_head struct of the instance. However,
                                            // if the offset of the list_head struct within the candidate is by chance
                                            // equal to the real candidate or if the offset is zero, we will pass the
                                            // check even though this may be the wrong candidate. This is why we use
                                            // back propagation to calculate the final probabilities.
                                            // _map->addChildIfNotExistend(m, node, _index, inst->memberAddress(i));
                                        }
                                        else {
                                            //debugmsg("Out 2: " << m.fullName());
                                            // Penalty of 90%
                                            penalize = 0.9;
                                        }
                                    }
                                    else
                                    {

                                        //debugmsg("Out 3: " << m.fullName() << " (" << candListHead.typeName() << ")");
                                        // Penalty of 90%
                                        penalize = 0.9;
                                    }
                                }
                            }


                        }

                        /*
                        if(inst->fullName().compare("tasks") == 0)
                           debugmsg("sibling Candidate: " << m.name());
                        */

                        // add node
                        int cnt = 1;
                        if (m.type()->type() == rtPointer)
                            m = m.dereference(BaseType::trLexicalPointersArrays, -1, &cnt);
                        if (cnt || (m.type()->type() & StructOrUnion)) {
                            // Adjust instance name to reflect full path
                            if (node->name() != inst->name())
                                m.setName(inst->name() + "." + m.name());

                            lastNode = _map->addChildIfNotExistend(m, node, _index, inst->memberAddress(i), true);

                            if(lastNode != NULL)
                            {
                                // Set penalty
                                float m_prob = lastNode->getInitialProbability() - penalize;

                                if(m_prob < 0)
                                    lastNode->setInitialProbability(0.0);
                                else
                                    lastNode->setInitialProbability(m_prob);
                            }
                        }
                    }
                }
                catch (GenericException& e) {
                    // Do nothing
                }
            }

            // Finish the canidate list.
            if(lastNode)
                lastNode->completeCandidates();
            //else
                ///debugmsg(inst->fullName() << " (" << inst->memberCandidatesCount(i) << ")");
        }

        // In case of a list_head we just consider the next pointer
        if(inst->isListHead())
            break;
#endif
    }
}


float MemoryMapBuilderSV::calculateNodeProbability(const Instance* inst,
                                                   float /*parentProbability*/) const
{
    // Degradation of 20% for address of this node not being aligned at 4 byte
    // boundary
    //static const float degForUnalignedAddr = 0.8;
    static const float degForUnalignedAddr = 1.0;

    // Degradation of 5% for address begin in userland
    static const float degForUserlandAddr = 0.95;

    // Degradation of 90% for an invalid address of this node
    static const float degForInvalidAddr = 0.1;

    // Degradation of 90% for an invalid list_head within this node
    // static const float degForInvalidListHead = 0.1;
    static const float degForInvalidListHead = 0.8;

    // Max. degradation of 30% for non-aligned pointer childen the type of this
    // node has
//    static const float degForNonAlignedChildAddr = 0.7;
    // static const float degForNonAlignedChildAddr = 1.0;

    // Max. degradation of 50% for invalid pointer childen the type of this node
    // has
//    static const float degForInvalidChildAddr = 0.5;
    //static const float degForInvalidChildAddr = 1.0;

    // Stores the final probability value
    float prob = 1.0;

    // Store the number of children that we verify to calculate a weighted
    // probability
    quint32 pointer = 0, listHeads = 0;


    // Check userland address
    if (inst->address() < _map->_vmem->memSpecs().pageOffset) {
        prob *= degForUserlandAddr;
        _map->_shared->degForUserlandAddrCnt++;
    }
    // Check validity
    else if (! _map->_vmem->safeSeek((qint64) inst->address()) ) {
        prob *= degForInvalidAddr;
        _map->_shared->degForInvalidAddrCnt++;
    }
    // Check alignment
    else if (inst->address() & 0x3ULL) {
        prob *= degForUnalignedAddr;
        _map->_shared->degForUnalignedAddrCnt++;
    }

    // Find the BaseType of this instance, dereference any lexical type(s)
    const BaseType* instType = inst->type() ?
            inst->type()->dereferencedBaseType(BaseType::trLexical) : 0;

    // If this a union or struct, we have to consider the pointer members
    if ( instType &&
         (instType->type() & StructOrUnion) )
    {
        const Structured* structured =
                dynamic_cast<const Structured*>(instType);

        // Store the invalid member count
        quint32 nonAlignedChildAddrCnt = 0, invalidChildAddrCnt = 0, invalidListHeadCnt = 0;


        // Check address of all descendant pointers
        for (MemberList::const_iterator it = structured->members().begin(),
             e = structured->members().end(); it != e; ++it)
        {
            const StructuredMember* m = *it;
            const BaseType* m_type = m->refTypeDeep(BaseType::trLexical);

            // Create an instance if possible
            Instance mi = Instance();
            try {
                mi = m->toInstance(inst->address(), _map->_vmem, inst);
            }
            catch(GenericException& ge)
            {
                // Address invalid
                mi = Instance();
            }


            if (m_type && (m_type->type() & rtPointer)) {
                pointer++;

                try {
                    quint64 m_addr = inst->address() + m->offset();
                    // Try a safeSeek first to avoid costly throws of exceptions
                    if (_map->_vmem->safeSeek(m_addr)) {
                        m_addr = (quint64)m_type->toPointer(_map->_vmem, m_addr);
                        // Check validity of non-null addresses
                        if (m_addr && !_map->_vmem->safeSeek((qint64) m_addr) ) {
                            invalidChildAddrCnt++;
                        }
                        // Check alignment
                        else if (m_addr & 0x3ULL) {
                            nonAlignedChildAddrCnt++;
                        }
                    }
                    else {
                        // Address was invalid
                        invalidChildAddrCnt++;
                    }
                }
                catch (MemAccessException&) {
                    // Address was invalid
                    invalidChildAddrCnt++;
                }
                catch (VirtualMemoryException&) {
                    // Address was invalid
                    invalidChildAddrCnt++;
                }
            }
            else if(!mi.isNull() && mi.isListHead())
            {
                listHeads++;

                // Check if a inst.list_head.next.prev pointer points to inst.list_head
                // as it should.
                Instance iListHeadNext = mi.member(0, 0, -1, true);
                if(!iListHeadNext.isNull() && iListHeadNext.type()
                        && _map->_vmem->safeSeek(iListHeadNext.address()))
                {
                    const BaseType *mNext = iListHeadNext.type();
                    quint64 listHeadNext =
                            (quint64)mNext->toPointer(_map->_vmem,
                                                      iListHeadNext.address());

                    // A list_head.next pointer may be NULL
                    /// todo: Check if this condition is already handled by !iListHeadNext.isNull()
                    /// in this case the else clause of the above if statement needs to be updated.
                    if(listHeadNext == 0)
                    {
                        continue;
                    }
                    else
                    {
                        // Check above mentioned condition
                        if(_map->_vmem->safeSeek(listHeadNext + mNext->size()))
                        {
                            quint64 listHeadNextPrev =
                                    (quint64)mNext->toPointer(_map->_vmem, listHeadNext + mNext->size());

                            if(mi.address() != listHeadNextPrev)
                            {
                                invalidListHeadCnt++;
                            }
                        }
                        else {
                            invalidListHeadCnt++;
                        }
                    }

                }
                else {
                    invalidListHeadCnt++;
                }
            }
        }

        // Penalize probabilities, weighted by number of meaningful children
        /*
        if (nonAlignedChildAddrCnt) {
            float invPart = nonAlignedChildAddrCnt / (float) pointer;
            prob *= invPart * degForNonAlignedChildAddr + (1.0 - invPart);
            degForNonAlignedChildAddrCnt++;
        }

        if (invalidChildAddrCnt) {
            float invPart = invalidChildAddrCnt / (float) pointer;
            prob *= invPart * degForInvalidChildAddr + (1.0 - invPart);
            degForInvalidChildAddrCnt++;
        }
        */

        // Penalize for invalid list_heads
        if (invalidListHeadCnt) {
            float invPart = invalidListHeadCnt / (float) listHeads;
            prob *= invPart * degForInvalidListHead + (1.0 - invPart);
            _map->_shared->degForInvalidListHeadCnt++;
        }


    }
    return prob;
}