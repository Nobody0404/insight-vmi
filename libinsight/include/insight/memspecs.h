/*
 * memspecs.h
 *
 *  Created on: 21.06.2010
 *      Author: chrschn
 */

#ifndef MEMSPECS_H_
#define MEMSPECS_H_

#include <QDataStream>
#include <QDateTime>
#include <debug.h>
#include "kernelsymbolstream.h"
#include "systemmapentry.h"

#define VADDR_SPACE_X86    0xFFFFFFFFUL
#define VADDR_SPACE_X86_64 0xFFFFFFFFFFFFFFFFULL

namespace str
{
extern const char* initLvl4Pgt;
extern const char* swapperPgDir;
extern const char* highMemory;
extern const char* vmallocEarlyres;
}


/**
 * This struct holds the definition of how a memory specification can be
 * extracted from a kernel source tree. It is used in conjunction with the
 * MemSpecs and MemSpecsParser classes.
 *
 * For this purpose, a small helper program
 * is compiled against the configured kernel source tree and then outputs the
 * desired information as a key-value pair in a specified printf() format.
 *
 * Suppose we want to know the value of the macro \c __PAGE_OFFSET of a kernel.
 * First, we set keyFmt to any self-explaining name, e. g.,
 * keyFmt="PAGE_OFFSET". This will be the key we have to look for in
 * MemSpecs::setFromKeyValue().
 *
 * Next, we have to define how the value can be
 * calculated from the kernel source in valueFmt. This can be any valid C
 * expression, including pre-processor macros. So for our example we set
 * valueFmt="__PAGE_OFFSET".
 *
 * Finally, we have to specify how the value should be output as string. This
 * is done by setting outputFmt to any valid format that printf will accept,
 * e. g., outputFmt="%16lx".
 *
 * To continue our example, the MemSpecsParser class will then create a helper
 * program which consists of the statement:
 *
 * \code
 * printf("PAGE_OFFSET=%16lx\n", __PAGE_OFFSET);
 * \endcode
 *
 * This program is compiled and run and the output is parsed and handed over
 * to a MemSpecs object by calling MemSpecs::setFromKeyValue().
 */
struct KernelMemSpec
{
    /// Constructor
    KernelMemSpec() {}

    /// Constructor
    KernelMemSpec(QString keyFmt, QString valueFmt, QString valueType,
                  QString outputFmt, QString macroCond = QString()) :
        keyFmt(keyFmt),
        valueFmt(valueFmt),
        valueType(valueType),
        outputFmt(outputFmt),
        macroCond(macroCond)
    {}

    /// The identifier (the key) of a key-value pair.
    QString keyFmt;

    /// A valid expression in C syntax to calcluate the value of a key-value pair.
    QString valueFmt;

    /// The dat atype of the value, e.g., <tt>long long</tt> or <tt>char*</tt>
    QString valueType;

    /// The output format in which the value should be printed (printf syntax).
    QString outputFmt;

    /// A conditional expression in C macro language that the evaluation is wrapped in
    QString macroCond;
};


/// A list of KernelMemSpec objects
typedef QList<KernelMemSpec> KernelMemSpecList;


/**
 * This struct holds the memory specifications, i.e., the fixed memory offsets
 * and locations as well as the CPU architecture.
 */
struct MemSpecs
{
    /// Architecture variants: i386 or x86_64
    enum Architecture {
        ar_undefined    = 0,        ///< architecture is not set
        ar_i386         = (1 << 0), ///< architecture is i386 with extended paging
        ar_x86_64       = (1 << 1), ///< architecture is AMD64
        ar_pae_enabled  = (1 << 2), ///< flag that indicates if PAE is enabled in i386 mode
        ar_i386_pae     = ar_i386 | ar_pae_enabled ///< architecture is i386 with PAE enabled
    };

    /**
     * Linux kernel version information, see Linux type "struct new_utsname"
     * in <include/linux/utsname.h>
     */
    struct Version
    {
        QString sysname;
        QString release;
        QString version;
        QString machine;
        bool equals(const Version& other) const;
        QString toString() const;
    };

    /// Constructor
    MemSpecs();

    /**
     * Sets any of the local member variables by parsing a key-value pair.
     * @param key the variable identifier
     * @param value the new value as string
     * @return \c true if the key-value pair could be parsed correctly,
     * \c false otherwise
     * \sa KernelMemSpec
     * \sa supportedMemSpecs()
     */
    bool setFromKeyValue(const QString& key, const QString& value);

    /**
     * @return a list of all kernel memory specifications that are supported
     * by setFromKeyValue().
     * \sa setFromKeyValue()
     */
    static KernelMemSpecList supportedMemSpecs();

    /**
     * Outputs these specs as key = value pairs, one by line
     * @return specs as string
     */
    QString toString() const;

    /**
     * Generates a unique file name based on the memory specifications and the
     * kernel version.
     * @return version-specific file name
     */
    QString toFileNameString() const;

    /**
     * @return the real (calculated) VMALLOC_START address that can actually
     * be is used for address translation
     */
    quint64 realVmallocStart() const;

    /**
     * @return the address of the last byte in virtual memory, i. e., either
     * \c 0xFFFFFFFF or \c 0xFFFFFFFFFFFFFFFF.
     */
    quint64 vaddrSpaceEnd() const;

    /**
     * Returns the information in the string-indexed systemMap hash in a more
     * easily processible QList.
     */
    SystemMapEntryList systemMapToList() const;

    quint64 pageOffset;
    quint64 vmallocStart;
    quint64 vmallocEnd;
    quint64 vmallocOffset;
    quint64 vmemmapStart;
    quint64 vmemmapEnd;
    quint64 modulesVaddr;
    quint64 modulesEnd;
    quint64 startKernelMap;
    quint64 initLevel4Pgt;
    quint64 swapperPgDir;
    quint64 highMemory;          ///< This is set at runtime by MemoryDump::init()
    quint64 vmallocEarlyreserve; ///< This is set at runtime by MemoryDump::init()
    quint64 listPoison1;
    quint64 listPoison2;
    qint32 maxErrNo;
    qint32 sizeofLong;
    qint32 sizeofPointer;
    qint32 arch;                 ///< An Architecture value
    QDateTime created;
    quint32 createdChangeClock;
    int symVersion;
    struct Version version;      ///< Linux kernel version information
    SystemMapEntries systemMap;  ///< all entries from the <tt>System.map</tt> file
    bool initialized;            ///< \c true after MemoryDump::init() is complete, \c false otherwise
};


inline quint64 MemSpecs::vaddrSpaceEnd() const
{
    return (arch & ar_x86_64) ? VADDR_SPACE_X86_64 : VADDR_SPACE_X86;
}


inline quint64 MemSpecs::realVmallocStart() const
{
    assert(initialized == true);
    if (arch & ar_i386)
        return (vmallocStart + highMemory + vmallocEarlyreserve) &
                ~(vmallocOffset - 1);
    else
        return vmallocStart;
}


/**
* Operator for native usage of the Variable class for streams
* @param in data stream to read from
* @param specs object to store the serialized data to
* @return the data stream \a in
*/
KernelSymbolStream& operator>>(KernelSymbolStream& in, MemSpecs& specs);


/**
* Operator for native usage of the Variable class for streams
* @param out data stream to write the serialized data to
* @param specs object to serialize
* @return the data stream \a out
*/
KernelSymbolStream& operator<<(KernelSymbolStream& out, const MemSpecs& specs);


#endif /* MEMSPECS_H_ */
