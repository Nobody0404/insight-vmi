/*
 * virtualmemory.h
 *
 *  Created on: 15.06.2010
 *      Author: chrschn
 */

#ifndef VIRTUALMEMORY_H_
#define VIRTUALMEMORY_H_

#include <QIODevice>
#include <QCache>
#include "genericexception.h"
#include "memspecs.h"

/**
 * This class provides read access to a virtual address space and performs
 * the virtual to physical address translation.
 */
class VirtualMemory: public QIODevice
{
public:
    VirtualMemory(MemSpecs specs, QIODevice* physMem = 0);
    virtual ~VirtualMemory();

    // Re-implementations of QIODevice
    virtual bool isSequential() const;
    virtual bool open (OpenMode mode);
    virtual qint64 pos() const;
    virtual bool reset();
    virtual bool seek (qint64 pos);
    virtual qint64 size() const;

    /**
     * @return the device containing the physical memory
     */
    const QIODevice* physMem() const;

    /**
     * Sets the device containing the physical memory
     * @param physMem new device containing physical memory
     */
    void setPhysMem(QIODevice* physMem);

protected:
    // Pure virtual functions of QIODevice
    virtual qint64 readData (char* data, qint64 maxSize);
    virtual qint64 writeData (const char* data, qint64 maxSize);

private:
    /**
     * Looks up a virtual address in the page table and returns the physical
     * address.
     * @param vaddr virtual address
     * @param pageSize here the size of the belonging page is returned
     * @return physical address
     */
    quint64 pageLookup(quint64 vaddr, int* pageSize);

    /**
     * Translates a virtual kernel address to a physical address, either by
     * linear translation or by page table look-up, depending on the address.
     * @param vaddr virtual address
     * @param pageSize here the size of the belonging page is returned
     * @return physical address
     */
    quint64 virtualToPhysical(quint64 vaddr, int* pageSize);

    /**
     * Reads a value of size _specs.sizeofUnsignedLong from memory and returns
     * it as 64 bit integer
     * @param physaddr the physical address to read from
     * @return the read value
     */
    quint64 extractULongFromPhysMem(quint64 physaddr);

    struct TLBEntry {
        TLBEntry(quint64 addr = 0, int size = 0)
            : addr(addr), size(size) {}
        quint64 addr;
        int size;
    };

    QCache<quint64, TLBEntry> _tlb;

    QIODevice* _physMem;
    MemSpecs _specs;
    quint64 _pos;
};

#endif /* VIRTUALMEMORY_H_ */