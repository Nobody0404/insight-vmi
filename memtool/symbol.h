/*
 * symbol.h
 *
 *  Created on: 05.04.2010
 *      Author: chrschn
 */

#ifndef SYMBOL_H_
#define SYMBOL_H_

#include "typeinfo.h"

/**
 * This class represents a generic debugging symbol read from the objdump output.
 */
class Symbol
{
public:
	/**
	 * Constructor
     * @param info the type information to construct this type from
	 */
	Symbol(const TypeInfo& info);

	/**
	 * Destructor
	 */
	virtual ~Symbol();

    /**
     * @return the name of that type, e.g. "int"
     */
    virtual QString name() const;

    /**
     * Set the name of this symbol
     * @param name new name
     */
    virtual void setName(const QString& name);

    /**
      @return id ID of this type, as given by objdump output
     */
    int id() const;

protected:
    const int _id;         ///< ID of this type, given by objdump
    QString _name;       ///< name of this type, e.g. "int"
};

#endif /* SYMBOL_H_ */