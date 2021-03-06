/*
 * memorydifftree.h
 *
 *  Created on: 14.12.2010
 *      Author: chrschn
 */

#ifndef MEMORYDIFFTREE_H_
#define MEMORYDIFFTREE_H_

// Forward declarations
template<class value_type, class value_accessor, class property_type>
class MemoryRangeTree;

#include "memorymapnode.h"
#include "memoryrangetree.h"

class Difference
{
public:
    Difference(qint64 startAddr = 0, qint64 runLength = 0)
        : startAddr(startAddr), runLength(runLength) {}

    quint64 startAddr;
    quint64 runLength;

    operator bool() const { return runLength; }
};


inline uint qHash(const Difference& diff)
{
    return qHash(diff.startAddr) ^ qHash(diff.runLength);
}


/**
 * This struct holds all interesting properties of Difference objects under a
 * MemoryRangeTreeNode.
 */
class DiffProperties
{
public:
    quint64 minRunLength;  ///< Minimal run-length of all differences
    quint64 maxRunLength;  ///< Maximal run-length of all differences
    int diffCount;         ///< No. of differences within this range

    DiffProperties() { reset(); }

    /**
     * Resets all data to initial state
     */
    void reset();

    /**
     * @return \c true if this range contains no objects, \c false otherwise
     */
    inline bool isEmpty() { return diffCount == 0; }

    /**
     * Update the properties with the ones from \a diff.
     * @param diff
     */
    void update(const Difference& diff);

    /**
     * Unites these properties with the ones found in \a other.
     * \warning Calling this function for properties of overlapping objects
     * may result in an inaccurate objectCount.
     * @param other the properties to unite with
     * @return reference to this object
     */
    DiffProperties& unite(const DiffProperties& other);
};

/**
 * Accessor class for Difference objects used by MemoryDiffTree.
 */
class DiffAccessor
{
public:
    static inline quint64 address(const Difference& diff)
    {
        return diff.startAddr;
    }

    static inline quint64 endAddress(const Difference& diff)
    {
        return diff.startAddr + diff.runLength - 1;
    }
};


typedef MemoryRangeTree<Difference, DiffAccessor, DiffProperties> MemoryDiffTree;
typedef MemoryDiffTree::ItemSet DiffSet;


#endif /* MEMORYDIFFTREE_H_ */
