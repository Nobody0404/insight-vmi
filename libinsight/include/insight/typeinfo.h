/*
 * typeinfo.h
 *
 *  Created on: 31.03.2010
 *      Author: chrschn
 */

#ifndef TYPEINFO_H_
#define TYPEINFO_H_

#include <QString>
#include <QMultiHash>
#include <QVariant>
#include <QVector>
#include <sys/types.h>
#include "memberlist.h"

/// These enum values represent all possible debugging symbol
enum HdrSymbolType {
	hsUnknownSymbol         = 0,         // Start with 0 for any unknown symbol
	hsArrayType             = (1 <<  0), // This represents first "real" symbol
	hsBaseType              = (1 <<  1),
	hsCompileUnit           = (1 <<  2),
	hsConstType             = (1 <<  3),
	hsEnumerationType       = (1 <<  4),
	hsEnumerator            = (1 <<  5),
	hsFormalParameter       = (1 <<  6),
	hsInlinedSubroutine     = (1 <<  7),
	hsLabel                 = (1 <<  8),
	hsLexicalBlock          = (1 <<  9),
	hsMember                = (1 << 10),
	hsPointerType           = (1 << 11),
	hsStructureType         = (1 << 12),
	hsSubprogram            = (1 << 13),
	hsSubrangeType          = (1 << 14),
	hsSubroutineType        = (1 << 15),
	hsTypedef               = (1 << 16),
	hsUnionType             = (1 << 17),
	hsUnspecifiedParameters = (1 << 18),
	hsVariable              = (1 << 19),
	hsVolatileType          = (1 << 20)
};

/// These enum values represent all possible parameters for a debugging symbol.
enum ParamSymbolType {
	psUnknownSymbol      = 0,         // Start with 0 for any unknown symbol
	psAbstractOrigin     = (1 <<  0), // This represents first "real" symbol
	psArtificial         = (1 <<  1),
	psBitOffset          = (1 <<  2),
	psBitSize            = (1 <<  3),
	psByteSize           = (1 <<  4),
	psCallFile           = (1 <<  5),
	psCallLine           = (1 <<  6),
	psCompDir            = (1 <<  7),
	psConstValue         = (1 <<  8),
	psDataMemberLocation = (1 <<  9),
	psDeclaration        = (1 << 10),
	psDeclFile           = (1 << 11),
	psDeclLine           = (1 << 12),
	psEncoding           = (1 << 13),
	psEntryPc            = (1 << 14),
	psExternal           = (1 << 15),
	psFrameBase          = (1 << 16),
	psHighPc             = (1 << 17),
	psInline             = (1 << 18),
	psLanguage           = (1 << 19),
	psLocation           = (1 << 20),
	psLowPc              = (1 << 21),
	psName               = (1 << 22),
	psProducer           = (1 << 23),
	psPrototyped         = (1 << 24),
	psRanges             = (1 << 25),
	psSibling            = (1 << 26),
	psStmtList           = (1 << 27),
	psType               = (1 << 28),
	psUpperBound         = (1 << 29)
};

/// The data encoding of a type
enum DataEncoding {
	eUndef     = 0,         // Start with 0 for any unknown symbol
	eSigned    = (1 << 0),  // This represents first "real" symbol
	eUnsigned  = (1 << 1),
	eBoolean   = (1 << 2),
	eFloat     = (1 << 3)
};

typedef QVector<qint32> IntVec;

typedef QHash<QString, HdrSymbolType> HdrSymMap;
typedef QHash<QString, ParamSymbolType> ParamSymMap;
typedef QHash<QString, DataEncoding> DataEncMap;

typedef QHash<HdrSymbolType, QString> HdrSymRevMap;
typedef QHash<ParamSymbolType, QString> ParamSymRevMap;

HdrSymMap getHdrSymMap();
ParamSymMap getParamSymMap();
DataEncMap getDataEncMap();

/**
 * Returns an inverted value of a QHash, i.e., a hash with key and value
 * types swapped.
 * @param hash the QHash to be inverted
 * @return an inverted version of @a hash
 */
template<class T>
QHash<typename T::mapped_type, typename T::key_type> invertHash(const T& hash)
{
    QHash<typename T::mapped_type, typename T::key_type> ret;
    for (typename T::const_iterator it = hash.begin(), e = hash.end();
         it != e; ++it)
    {
        ret.insert(it.value(), it.key());
    }
    return ret;
}


static const quint32 RelevantHdr =
	hsArrayType |
	hsBaseType |
	hsCompileUnit |
	hsConstType |
	hsEnumerationType |
	hsEnumerator |
//	hsFormalParameter |
//	hsInlinedSubroutine |
//	hsLabel |
//	hsLexicalBlock |
	hsMember |
	hsPointerType |
	hsStructureType |
	hsSubprogram |
	hsSubrangeType |
	hsSubroutineType |
	hsTypedef |
	hsUnionType |
//	hsUnspecifiedParameters |
	hsVariable |
	hsVolatileType;


static const quint32 SubHdrTypes =
    hsSubrangeType |
    hsEnumerator |
    hsMember |
    hsFormalParameter;


static const quint32 RelevantParam =
	psAbstractOrigin |
//	psArtificial |
	psBitOffset |
	psBitSize |
	psByteSize |
//	psCallFile |
//	psCallLine |
	psCompDir |
	psConstValue |
	psDataMemberLocation |
//	psDeclaration |
	psDeclFile |
	psDeclLine |
	psEncoding |
//	psEntryPc |
	psExternal |
//	psFrameBase |
	psHighPc |
	psInline |
//	psLanguage |
	psLocation |
	psLowPc |
	psName |
//	psProducer |
//	psPrototyped |
//	psRanges |
	psSibling |
//	psStmtList |
	psType |
	psUpperBound;

// forward declaration
class FuncParam;

/// A list of FuncParam objects
typedef QList<FuncParam*> ParamList;

/**
 * Holds all information about a parsed debugging symbol. Its main purpose is
 * to transfer the symbol information from the parser to the SymFactory class.
 */
class TypeInfo
{
public:
	typedef QMultiHash<qint32, QString> EnumHash;

	/**
	 * Constructor
	 * @param fileIndex index of the symbol file where this information was
	 * collected from
	 */
	TypeInfo(int fileIndex);

	/**
	 * Resets all data to their default (empty) values
	 */
	void clear();

	void deleteParams();

	int fileIndex() const;
	void setFileIndex(int index);

	bool isRelevant() const;
	void setIsRelevant(bool value);

    const QString& name() const;
    void setName(QString name);

    const QString& srcDir() const;
    void setSrcDir(QString name);

    int srcFileId() const;
    void setSrcFileId(int id);

    int srcLine() const;
    void setSrcLine(int lineno);

    int id() const;
    void setId(int id);

    int origId() const;
    void setOrigId(int id);

    int refTypeId() const;
    void setRefTypeId(int refTypeID);

    quint32 byteSize() const;
    void setByteSize(quint32 byteSize);

    int bitSize() const;
    void setBitSize(int bitSize);

    int bitOffset() const;
    void setBitOffset(int bitOffset);

    quint64 location() const;
    void setLocation(quint64 location);

    bool hasLocation() const;

    qint32 dataMemberLocation() const;
    void setDataMemberLocation(qint32 location);

    HdrSymbolType symType() const;
    void setSymType(HdrSymbolType symType);

    DataEncoding enc() const;
    void setEnc(DataEncoding enc);

    const IntVec &upperBounds() const;
    void setUpperBounds(const IntVec& bounds);
    void addUpperBounds(const IntVec& bounds);
    void addUpperBound(qint32 bound);

    int external() const;
    void setExternal(int value);

    qint32 sibling() const;
    void setSibling(qint32 sibling);

    bool inlined() const;
    void setInlined(bool value);

    quint64 pcLow() const;
    void setPcLow(quint64 pc);

    quint64 pcHigh() const;
    void setPcHigh(quint64 pc);

    QVariant constValue() const;
    void setConstValue(QVariant value);

    const EnumHash& enumValues() const;
    void addEnumValue(const QString& name, qint32 value);

    MemberList& members();
    const MemberList& members() const;

    ParamList& params();
    const ParamList& params() const;

    const QString& section() const;
    void setSection(const QString& section);

    QString dump(const QStringList &symFiles) const;

private:
	int _fileIndex;          ///< index of the object dump containing the info
	bool _isRelevant;
	QString _name;           ///< holds the name of this symbol
	QString _srcDir;         ///< holds the directory of the compile unit
	int _srcFileId;          ///< holds the ID of the source file
	int _srcLine;            ///< holds the line number within the source file
	int _id;                 ///< holds the ID of this symbol
	int _origId;             ///< holds the original ID of this symbol
	int _refTypeId;          ///< holds the ID of the referenced symbol
	quint32 _byteSize;       ///< holds the size in byte of this symbol
	int _bitSize;            ///< holds the number of bits for a bit-split struct
	int _bitOffset;          ///< holds the bit offset for a bit-split struct
	quint64 _location;       ///< holds the absolute offset offset of this symbol
	bool _hasLocation;   ///< is set to true when a location was set
	int _external;			 ///< holds whether this is an external symbol
	qint32 _dataMemberLoc;   ///< holds the offset relative offset of this symbol
	IntVec _upperBounds;     ///< holds the upper bounds for an integer type symbol
	qint32 _sibling;         ///< holds the sibling for a subprogram type symbol
	bool _inlined;           ///< was the function inlined?
	quint64 _pcLow;          ///< low program counter of a function
	quint64 _pcHigh;         ///< high program counter of a function
	QVariant _constValue;    ///< holds the value of an enumerator symbol
	EnumHash _enumValues; ///< holds the enumeration values, if this symbol is an enumeration
	HdrSymbolType _symType;  ///< holds the type of this symbol
	DataEncoding _enc;       ///< holds the data encoding of this symbol
	MemberList _members;     ///< holds all members of a union or struct
	ParamList _params;       ///< holds all parameters of a function (pointer)
	QString _section;        ///< holds the segment name for a variable
};


inline int TypeInfo::fileIndex() const
{
	return _fileIndex;
}


inline void TypeInfo::setFileIndex(int index)
{
	_fileIndex = index;
}


inline bool TypeInfo::isRelevant() const
{
	return _isRelevant;
}


inline void TypeInfo::setIsRelevant(bool value)
{
	_isRelevant = value;
}


inline const QString& TypeInfo::name() const
{
	return _name;
}


inline void TypeInfo::setName(QString name)
{
	this->_name = name;
}


inline const QString& TypeInfo::srcDir() const
{
	return _srcDir;
}


inline void TypeInfo::setSrcDir(QString dir)
{
	_srcDir = dir;
}

inline int TypeInfo::srcFileId() const
{
	return _srcFileId;
}


inline void TypeInfo::setSrcFileId(int id)
{
	_srcFileId = id;
}


inline int TypeInfo::srcLine() const
{
	return _srcLine;
}


inline void TypeInfo::setSrcLine(int lineno)
{
	_srcLine = lineno;
}


inline int TypeInfo::id() const
{
	return _id;
}


inline void TypeInfo::setId(int id)
{
	this->_id = id;
}


inline int TypeInfo::origId() const
{
	return _origId;
}


inline void TypeInfo::setOrigId(int id)
{
	this->_origId = id;
}


inline int TypeInfo::refTypeId() const
{
	return _refTypeId;
}


inline void TypeInfo::setRefTypeId(int refTypeID)
{
	this->_refTypeId = refTypeID;
}


inline quint32 TypeInfo::byteSize() const
{
	return _byteSize;
}


inline void TypeInfo::setByteSize(quint32 byteSize)
{
	this->_byteSize = byteSize;
}


inline int TypeInfo::bitSize() const
{
	return _bitSize;
}


inline void TypeInfo::setBitSize(int bitSize)
{
	_bitSize = bitSize;
}


inline int TypeInfo::bitOffset() const
{
	return _bitOffset;
}


inline void TypeInfo::setBitOffset(int bitOffset)
{
	_bitOffset = bitOffset;
}


inline quint64 TypeInfo::location() const
{
	return _location;
}


inline void TypeInfo::setLocation(quint64 location)
{
    this->_location = location;
    this->_hasLocation = true;
}


inline bool TypeInfo::hasLocation() const
{
    return _hasLocation;
}


inline qint32 TypeInfo::dataMemberLocation() const
{
    return _dataMemberLoc;
}


inline void TypeInfo::setDataMemberLocation(qint32 location)
{
    this->_dataMemberLoc = location;
}


inline HdrSymbolType TypeInfo::symType() const
{
    return _symType;
}


inline void TypeInfo::setSymType(HdrSymbolType symType)
{
    this->_symType = symType;
}


inline DataEncoding TypeInfo::enc() const
{
    return _enc;
}


inline void TypeInfo::setEnc(DataEncoding enc)
{
    this->_enc = enc;
}


inline const IntVec& TypeInfo::upperBounds() const
{
    return _upperBounds;
}


inline void TypeInfo::setUpperBounds(const IntVec& bounds)
{
    _upperBounds = bounds;
}


inline void TypeInfo::addUpperBounds(const IntVec& bounds)
{
    _upperBounds += bounds;
}


inline void TypeInfo::addUpperBound(qint32 bound)
{
    _upperBounds.append(bound);
}


inline int TypeInfo::external() const
{
    return _external;
}


inline void TypeInfo::setExternal(int value)
{
    _external = value;
}


inline qint32 TypeInfo::sibling() const
{
    return _sibling;
}


inline void TypeInfo::setSibling(qint32 sibling)
{
    _sibling = sibling;
}


inline bool TypeInfo::inlined() const
{
    return _inlined;
}


inline void TypeInfo::setInlined(bool value)
{
    _inlined = value;
}


inline quint64 TypeInfo::pcLow() const
{
    return _pcLow;
}


inline void TypeInfo::setPcLow(quint64 pc)
{
    _pcLow = pc;
}


inline quint64 TypeInfo::pcHigh() const
{
    return _pcHigh;
}


inline void TypeInfo::setPcHigh(quint64 pc)
{
    _pcHigh = pc;
}

// TODO: Currently QVariant is returned
inline QVariant TypeInfo::constValue() const
{
    return _constValue;
}


inline void TypeInfo::setConstValue(QVariant value)
{
    _constValue = value;
}


inline const TypeInfo::EnumHash& TypeInfo::enumValues() const
{
    return _enumValues;
}


inline void TypeInfo::addEnumValue(const QString& name, qint32 value)
{
    _enumValues.insertMulti(value, name);
}


inline MemberList& TypeInfo::members()
{
    return _members;
}


inline const MemberList& TypeInfo::members() const
{
    return _members;
}


inline ParamList& TypeInfo::params()
{
    return _params;
}


inline const ParamList& TypeInfo::params() const
{
    return _params;
}


inline const QString &TypeInfo::section() const
{
    return _section;
}

inline void TypeInfo::setSection(const QString &section)
{
    _section = section;
}


#endif /* TYPEINFO_H_ */
