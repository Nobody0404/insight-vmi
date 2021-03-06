/*
 * memorymap.h
 *
 *  Created on: 27.08.2010
 *      Author: chrschn
 */

#ifndef MEMORYMAP_H_
#define MEMORYMAP_H_

#include <QString>
#include <QSet>
#include <QMultiHash>
#include <QMultiMap>
#include <QPair>
#include <QVector>
#include <QMutex>
#include <QWaitCondition>
#include <QReadWriteLock>
#include "memorymapnode.h"
#include "priorityqueue.h"
#include "memorymaprangetree.h"
#include "memorydifftree.h"
#include "slubobjects.h"
#include "memorymapverifier.h"
#include "memorymapbuildercs.h"
#include "memorymapbuildersv.h"
#include "longoperation.h"
#include <debug.h>

class SymFactory;
class KernelSymbols;
class VirtualMemory;
class Variable;
class MemoryMapVerifier;
class MemoryMapBuilder;

/// A set of strings
typedef QSet<QString> StringSet;

/// A set of long integers
typedef QSet<quint64> ULongSet;

/// A address-indexed hash of MemoryMapNode pointers
typedef QMultiHash<quint64, MemoryMapNode*> PointerNodeHash;

/// An integer-indexed hash of MemoryMapNode pointers
typedef QMultiHash<int, MemoryMapNode*> IntNodeHash;

/// A pair of an integer and a MemoryMapNode pointer
typedef QPair<int, MemoryMapNode*> IntNodePair;

/// An address-indexed map of pairs of an integer and a MemoryMapNode pointer
typedef QMultiMap<quint64, IntNodePair> PointerIntNodeMap;

/// Holds the nodes to be visited, sorted by their probability
typedef PriorityQueue<float, MemoryMapNode*> NodeQueue;

#define MAX_BUILDER_THREADS 8

/**
 * Holds all variables that are shared among the builder threads.
 * \sa MemoryMapBuilder
 */
struct BuilderSharedState
{
    BuilderSharedState()
    {
        reset();
    }

    void reset()
    {
        queue.clear();
        minProbability = 0;
        processed = threadCount = 0;
        maxObjSize = 0;
        lastNode = 0;
        for (int i = 0; i < MAX_BUILDER_THREADS; ++i)
            currAddresses[i] = 0;
    }

    float minProbability;
    int threadCount;
    unsigned int maxObjSize;
    quint64 currAddresses[MAX_BUILDER_THREADS];
    QMutex perThreadLock[MAX_BUILDER_THREADS];
    QWaitCondition threadDone[MAX_BUILDER_THREADS];
    NodeQueue queue;
    MemoryMapNode* lastNode;
    qint64 processed;
    QMutex queueLock;
    QMutex functionPointersLock;
    QReadWriteLock pmemMapLock, vmemMapLock, currAddressesLock,
        typeInstancesLock, pointersToLock;

#ifdef DEBUG
	mutable quint32 degPerGenerationCnt;
	mutable quint32 degForUnalignedAddrCnt;
	mutable quint32 degForUserlandAddrCnt;
	mutable quint32 degForInvalidAddrCnt;
	mutable quint32 degForNonAlignedChildAddrCnt;
	mutable quint32 degForInvalidChildAddrCnt;
	mutable quint32 degForInvalidListHeadCnt;
#endif
};

/**
 * Holds information about all function pointers contained
 * within a specific \sa MemoryMapNode.
 */
struct FuncPointersInNode
{
    FuncPointersInNode() : node(0) {}
    FuncPointersInNode(const MemoryMapNode *node,
                       VariableTypeContainerList *path) :
        node(node)
    {
        if (path)
            paths.append((*path));
    }

    const MemoryMapNode *node;
    QList<VariableTypeContainerList> paths;
};


/**
 * This class represents a map of used virtual and physical memory. It allows
 * fast answers to queries like "which struct resides at virtual/physical
 * address X".
 */
class MemoryMap: protected LongOperation
{
    friend class MemoryMapBuilderCS;
    friend class MemoryMapBuilderSV;
public:
	/**
	 * Constructor
	 * @param symbols the symbols to use
	 * @param vmem the virtual memory instance to build a mapping from
	 */
	MemoryMap(KernelSymbols* symbols, VirtualMemory* vmem);

	/**
	 * Destructor
	 */
	virtual ~MemoryMap();

	/**
	 * Deletes all existing data.
	 */
	void clear();

    /**
     * Deletes all existing data of the reverse map and prepares it for a
     * re-built.
     */
    void clearRevmap();

    /**
     * Deletes the data in the difference tree.
     */
    void clearDiff();

	/**
	 * Builds up the memory mapping for the virtual memory object previously
	 * specified in the constructor.
	 * \note This might take a while.
	 * @param type which builder type to use
	 * @param minProbability stop building when the node's probability drops
	 *  below this threshold
	 */
	void build(MemoryMapBuilderType type, float minProbability,
			   const QString &slubObjFile = QString());

	bool dump(const QString& fileName) const;
    void dumpInitHelper(QTextStream &out, MemoryMapNode *node, quint32 curLvl, const quint32 level) const;
    bool dumpInit(const QString &fileName, const quint32 level = 3) const;

	/**
	 * Finds the differences in physical memory between this and another memory
	 * map
	 * @param other
	 */
	void diffWith(MemoryMap* other);

	/**
	 * @return the virtual memory object this mapping is built for
	 */
    VirtualMemory* vmem();

	/**
	 * @return the virtual memory object this mapping is built for (const
	 * version)
	 */
    const VirtualMemory* vmem() const;

    /**
     * Returns a list of all MemoryMapNode objects that served as "entry" into
     * the memory map graph. In essence, these nodes represent all global
     * kernel variables.
     * @return a list of all "root" MemoryMapNode's
     *
     */
    const NodeList& roots() const;

    /**
     * This gives access to all allocated objects in the virtual address space,
     * indexed by their address. Use vmemMap().lowerBound() or
     * vmemMap().upperBound() to find an object that is closest to a given
     * address.
     * @return the map of allocated kernel objects in virtual memory
     */
    const MemoryMapRangeTree& vmemMap() const;

    /**
     * @return the address of the last byte in virtual memory, i. e., either
     * \c 0xFFFFFFFF or \c 0xFFFFFFFFFFFFFFFF.
     */
    quint64 vaddrSpaceEnd() const;

    /**
     * @return the address of the last byte in physical memory
     */
    quint64 paddrSpaceEnd() const;

    /**
     * Finds all objects in virtual memory that occupy space between
     * \a addrStart and \a addrEnd. Objects that only partly fall into that
     * range are included.     *
     * @param addrStart the virtual start address
     * @param addrEnd the virtual end address (including)
     * @return a list of MemoryMapNode objects
     */
    MemMapSet vmemMapsInRange(quint64 addrStart, quint64 addrEnd) const;

    /**
     * This gives access to all allocated objects and the page size of the
     * corresponding page in the physical address space, indexed by their
     * address. Use pmemMap().lowerBound() or pmemMap().upperBound() to find an
     * object that is closest to a given address.
     * \note If objects span over multiple pages, they may occur multiple times
     * in this mapping.
     * @return the map of allocated kernel objects in physical memory
     */
    const PhysMemoryMapRangeTree& pmemMap() const;

    /**
     * Gives access to the difference map that was built using diffWith()
     * @return the differnece map
     */
    const MemoryDiffTree& pmemDiff() const;

    /**
     * This data structure allows to query which object(s) or pointer(s) point
     * to a given address.
     * @return an address-indexed hash of objects that point to that address
     */
    const PointerNodeHash& pointersTo() const;

    /**
     * This data structure allows to access all function pointers that are
     * contained within the map.
     * @return a list of FuncPointersInNode structures that contain the
     * MemoryMapNode as well as a list of MemberLists that can be used to
     * retrieve the individual function pointers.
     */
    const QList<FuncPointersInNode> & funcPointers() const;

    /**
     * This property indicates whether the memory map is currently being built.
     * @return \c true if the build process is in progress, \c false otherwise
     */
    bool isBuilding() const;

    /**
     * Inserts the given name \a name in the static list of names and returns
     * a reference to it. This static function was introduced to only hold one
     * copy of any kernel object name in memory, thus saving memory.
     * @param name the name to insert into the static name list
     * @return a constant reference to the inserted name
     */
    static const QString& insertName(const QString& name);

    /**
     * Returns the MemoryMapVerifier that is used by this map.
     */
    MemoryMapVerifier& verifier();

    /**
     * Returns the SymFactory used by this map.
     */
    const SymFactory * factory() const;

    /**
     * Returns the KernelSymbols used by this map.
     */
    KernelSymbols* symbols() const;

    float calculateNodeProbability(const Instance &inst,
                                   float parentProbability = 1.0) const;

    bool useRuleEngine() const;

    KnowledgeSources knowSrc() const;

    MemoryMapBuilderType buildType() const;

    /**
     * Returns \c true if changes to a node's probability should be propagated
     * recursively to its parents.
     */
    bool probabilityPropagation() const;

    ConstNodeList nodesOfType(int id) const;

protected:
    virtual void operationProgress();

private:
    /// Holds the static list of kernel object names. \sa insertName()
	static StringSet _names;

    /**
     * Checks if a given Instance object already exists in the virtual memory
     * mapping. If an instance at the same address already exists, the
     * BaseType::hash() of the instance's type is compared. Only if address and
     * hash match, the instance is considered to be already existent.
     * @param origInst the Instance object to check for existence
     * @param candidates a list of candidates, one of which might be valid
     *  instead of \a origInst
     * @return existing Node if an instance already exists at the address of \a inst
     * and the hash of both types are equal, null otherwise
     */
    MemoryMapNode* existsNode(const Instance& origInst,
                              const InstanceList &candidates) const;

    MemMapList findAllNodes(const Instance& origInst,
                            const InstanceList &candidates) const;

   /**
    * Check is existance of \a inst is plausible with current virtual memory mapping.
    * @param origInst the Instance object to check for existence
    * @param candidates a list of candidates, one of which might be valid
    * @param parent the parent node of \a inst
    * @return true if object inst is plausible, false otherwise
    */
    bool objectIsSane(const Instance& inst) const;

	/**
	 * Adds a new node for Instance \a inst as child of \a node, if \a inst is
	 * not already contained in the virtual memory mapping. If a new node is
	 * added, it is also appended to the queue \a queue for further processing.
	 * @param origInst the unmodified instance to create a new node from
	 * @param candidates a list of candidates (i.e., modified instances), one of
	 * which might be valid in instead of \a origInst
	 * @param parent the parent node of the new node to be created
     * @param addrInParent the address of the member within the parent
     * @param addToQueue should the new node be added to the queue
     * @return \c A pointer to the new node if the node could be added, \c NULL
     * if that instance is in conflict to already existing nodes and a pointer to
     * the existing node, if it already existed in the virtual memory mapping
	 */
	MemoryMapNode* addChildIfNotExistend(const Instance& origInst,
										 const InstanceList& candidates,
										 MemoryMapNode* parent,
										 int threadIndex, quint64 addrInParent,
										 bool addToQueue = true);

	MemoryMapNode* addChild(const Instance& origInst,
							const InstanceList& candidates,
							MemoryMapNode* parent, int threadIndex,
							quint64 addrInParent, bool addToQueue = true);

	bool shouldEnqueue(const Instance& inst, MemoryMapNode* node) const;

    /**
     * Check if at least one builder is still runnning.
     * @returns true if there is an active MemoryMapBuilder, false otherwise.
     */
    bool builderRunning() const;

    /**
     * Returns the list of per-cpu offsets.
     * \warning This code is entirely Linux-specific and is superseded by the
     * logic of the TypeRuleEngine. Its usage is discuraged!
     * @return
     */
    QVector<quint64> perCpuOffsets();

    /**
     * \warning This function is \b not thread-safe! It must not be called in
     * multi-treaded contexts!
     * @param var variable to add
     */
    void addVariableWithCandidates(const Variable* var);

    /**
     * \warning This function is \b not thread-safe! It must not be called in
     * multi-treaded contexts!
     * @param var variable to add
     */
    void addVariableWithRules(const Variable* var);

    /**
     * \warning This function is \b not thread-safe! It must not be called in
     * multi-treaded contexts!
     * @param inst root instance to add
     */
    void addVarInstance(const Instance& inst);

    void addNodeToHashes(MemoryMapNode *node);

    QStringList loadedKernelModules();

    MemoryMapBuilder** _threads;
    KernelSymbols* _symbols;     ///< holds the KernelSymbols to operate on
    VirtualMemory* _vmem;        ///< the virtual memory object this map is being built for
	NodeList _roots;             ///< the nodes of the global kernel variables
    PointerNodeHash _pointersTo; ///< holds all pointers that point to a certain address
    QList<FuncPointersInNode> _funcPointers; ///< holds all function pointers
    IntNodeHash _typeInstances;  ///< holds all instances of a given type ID
    MemoryMapRangeTree _vmemMap; ///< map of all used kernel-space virtual memory
    PhysMemoryMapRangeTree _pmemMap; ///< map of all used physical memory
    MemoryDiffTree _pmemDiff;    ///< differences between this and another map
    ULongSet _vmemAddresses;     ///< holds all virtual addresses
    bool _isBuilding;            ///< indicates if the memory map is currently being built
    BuilderSharedState* _shared; ///< all variables that are shared among the builder threads
    bool _useRuleEngine;
    KnowledgeSources _knowSrc;
    QVector<quint64> _perCpuOffset;
    MemoryMapBuilderType _buildType;
    bool _probPropagation;
    int _prevQueueSize;

#if MEMORY_MAP_VERIFICATION == 1
    MemoryMapVerifier _verifier; ///< provides debug checks for the creation of the memory map
#endif
};

//------------------------------------------------------------------------------

inline VirtualMemory* MemoryMap::vmem()
{
    return _vmem;
}


inline const VirtualMemory* MemoryMap::vmem() const
{
    return _vmem;
}


inline const NodeList& MemoryMap::roots() const
{
    return _roots;
}


inline const MemoryMapRangeTree& MemoryMap::vmemMap() const
{
    return _vmemMap;
}


inline const PhysMemoryMapRangeTree& MemoryMap::pmemMap() const
{
    return _pmemMap;
}


inline const MemoryDiffTree& MemoryMap::pmemDiff() const
{
    return _pmemDiff;
}


inline const PointerNodeHash& MemoryMap::pointersTo() const
{
    return _pointersTo;
}

inline const QList<FuncPointersInNode> & MemoryMap::funcPointers() const
{
    return _funcPointers;
}

inline bool MemoryMap::isBuilding() const
{
    return _isBuilding;
}


inline MemMapSet MemoryMap::vmemMapsInRange(quint64 addrStart, quint64 addrEnd) const
{
    return _vmemMap.objectsInRange(addrStart, addrEnd);
}


inline quint64 MemoryMap::vaddrSpaceEnd() const
{
    return _vmem ? _vmem->memSpecs().vaddrSpaceEnd() : VADDR_SPACE_X86;
}


inline quint64 MemoryMap::paddrSpaceEnd() const
{
    return _vmem && _vmem->physMem() && _vmem->physMem()->size() > 0 ?
            _vmem->physMem()->size() - 1 : VADDR_SPACE_X86;
}

#ifdef MEMORY_MAP_VERIFICATION
inline MemoryMapVerifier& MemoryMap::verifier()
{
    return _verifier;
}
#endif

inline float MemoryMap::calculateNodeProbability(const Instance &inst,
                                                 float parentProbability) const
{
    if (_threads && _threads[0])
        return _threads[0]->calculateNodeProbability(inst, parentProbability);

    return 1.0;
}

inline bool MemoryMap::useRuleEngine() const
{
    return _useRuleEngine;
}

inline KnowledgeSources MemoryMap::knowSrc() const
{
    return _knowSrc;
}


inline MemoryMapBuilderType MemoryMap::buildType() const
{
    return _buildType;
}


inline bool MemoryMap::probabilityPropagation() const
{
    return _probPropagation;
}


inline KernelSymbols *MemoryMap::symbols() const
{
    return _symbols;
}


#endif /* MEMORYMAP_H_ */
