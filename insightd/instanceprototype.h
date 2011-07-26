/*
 * instanceprototype.h
 *
 *  Created on: 05.07.2010
 *      Author: chrschn
 */

#ifndef INSTANCEPROTOTYPE_H_
#define INSTANCEPROTOTYPE_H_

#include <QObject>
#include <QScriptable>
#include <QStringList>
#include "instance.h"
#include "genericexception.h"
#include "instancedata.h"


/**
 * This class is the prototype for script variables of type Interface.
 *
 * All functions of this class are implemented as slots, they all just forward
 * the function invokation to the underlying Instance object, obtained by
 * thisInstance().
 *
 * Only exceptions: As ECMA-262 only supports \c int32 and \c uint32, a
 * \c (u)int64 in QtScript is automatically castet to a \a double value, which
 * leads to a loss of precision. The only value that is affected by this
 * problem is the instance's address. We handle this problem by providing three
 * access functions to the address:
 *
 * \li address() returns the address as zero-padded string in hex format
 * \li addressHigh() returns the most significant 32 bits of the address as \c uint32
 * \li addressLow() returns the least significant 32 bits of the address as \c uint32
 *
 * \sa Instance
 * \sa InstanceClass
 */
class InstancePrototype : public QObject, public QScriptable
{
    Q_OBJECT
public:
    InstancePrototype(QObject *parent = 0);
    virtual ~InstancePrototype();

public slots:
	/**
	 * @return the address as zero-padded string in hex format
     * \sa AddressLow(), AddressHigh()
	 */
    QString Address() const;

    /**
     * Sets the address of this instance to the given address. The address must
     * be given as hex encoded string, which may optionally start with "0x".
     * @param addr the hex enocded address to set
     */
    void SetAddress(QString addr);

    /**
     * @return the most significant 32 bits of the address as \c uint32
     * \sa AddressLow(), Address()
     */
    quint32 AddressHigh() const;

    /**
     * Sets the most significant 32 bits of the address.
     * @param addrHigh the most significant 32 bits of the address
     */
    void SetAddressHigh(quint32 addrHigh);

    /**
     * @return the least significant 32 bits of the address as \c uint32
     * \sa AddressHigh(), Address()
     */
    quint32 AddressLow() const;

    /**
     * Sets the least significant 32 bits of the address.
     * @param addrLow the least significant 32 bits of the address
     */
    void SetAddressLow(quint32 addrLow);

    /**
     * Adds \a offset to the virtual address.
     * @param offset the offset to add
     */
    void AddToAddress(int offset);

    int Id() const;
    int MemDumpIndex() const;
    QString Name() const;
    QString ParentName() const;
    QString FullName() const;
    QStringList MemberNames() const;
    InstanceList Members() const;
    QString Type() const;
    QString TypeName() const;
    quint32 Size() const;
    bool MemberExists(const QString& name) const;
    int MemberOffset(const QString& name) const;
    Instance FindMember(const QString& name) const;
    int TypeIdOfMember(const QString& name) const;
    int PointerSize() const;
    bool Equals(const Instance& other) const;
    QStringList Differences(const Instance& other, bool recursive) const;
    bool IsAddressNull() const;
    bool IsNull() const;
    bool IsAccessible() const;
    bool IsNumber() const;
    bool IsInteger() const;
    bool IsReal() const;
    void ChangeType(const QString& typeName);
    void ChangeType(int typeId);

    qint8 toInt8() const;
    quint8 toUInt8() const;
    qint16 toInt16() const;
    quint16 toUInt16() const;
    qint32 toInt32() const;
    quint32 toUInt32() const;
    QString toInt64(int base = 10) const;
    QString toUInt64(int base = 10) const;
    quint32 toUInt64High() const;
    quint32 toUInt64Low() const;
    float toFloat() const;
    double toDouble() const;
    QString toString() const;

    QString derefUserLand(const QString &pgd) const;

private:
    inline Instance* thisInstance() const;
    inline void injectScriptError(const GenericException& e) const;
    inline void injectScriptError(const QString& msg) const;
};

#endif /* INSTANCEPROTOTYPE_H_ */