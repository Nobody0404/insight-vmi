/*
 * volatiletype.h
 *
 *  Created on: 05.04.2010
 *      Author: chrschn
 */

#ifndef VOLATILETYPE_H_
#define VOLATILETYPE_H_

#include "refbasetype.h"

class VolatileType: public RefBaseType
{
public:
    /**
     * Constructor
     * @param symbols the kernel symbols this symbol belongs to
     */
    VolatileType(KernelSymbols* symbols);

    /**
     * Constructor
     * @param symbols the kernel symbols this symbol belongs to
     * @param info the type information to volatileruct this type from
     */
    VolatileType(KernelSymbols* symbols, const TypeInfo& info);

	/**
	 @return the actual type of that polimorphic variable
	 */
	virtual RealType type() const;

    /**
     * \copydoc Symbol::prettyName()
     */
    virtual QString prettyName(const QString& varName = QString()) const;

//    /**
//     * @param mem the memory device to read the data from
//     * @param offset the offset at which to read the value from memory
//     * @return a string representation of this type
//     */
//    virtual QString toString(QIODevice* mem, size_t offset, const ColorPalette* col = 0) const;
};


#endif /* VOLATILETYPE_H_ */
