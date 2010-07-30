/*
 * programoptions.cpp
 *
 *  Created on: 01.06.2010
 *      Author: chrschn
 */

#include "programoptions.h"
#include <iostream>
#include <iomanip>
#include <QFileInfo>
#include <QCoreApplication>

ProgramOptions programOptions;

const int OPTION_COUNT = 5;

const struct Option options[OPTION_COUNT] = {
        {
                "-d",
                "--daemon",
                "Start as a background process and detach console",
                acNone,
                opDaemonize,
                ntOption,
                0 // conflicting options
        },
        {
                "-p",
                "--parse",
                "Parse the debugging symbols from the given objdump file",
                acParseSymbols,
                opNone,
                ntInFileName,
                0 // conflicting options
        },
        {
                "-l",
                "--load",
                "Read in previously saved debugging symbols",
                acLoadSymbols,
                opNone,
                ntInFileName,
                0 // conflicting options
        },
        {
                "-m",
                "--memory",
                "Load a memory dump",
                acNone,
                opNone,
                ntMemFileName,
                0 // conflicting options
        },
        {
                "-h",
                "--help",
                "Show this help",
                acUsage,
                opNone,
                ntOption,
                0
        }
};


const char* history_file = ".memtool/history";
const char* lock_file = ".memtool/memtool.lock";
const char* log_file = ".memtool/memtool.log";
const char* sock_file = ".memtool/memtool.sock";


//------------------------------------------------------------------------------

ProgramOptions::ProgramOptions()
    : _action(acNone), _activeOptions(0)
{
}


void ProgramOptions::cmdOptionsUsage()
{
    QString appName = QCoreApplication::applicationFilePath();
    appName = appName.right(appName.length() - appName.lastIndexOf('/') - 1);

    std::cout
        << "Usage: " << appName.toStdString() << " [options]" << std::endl
        << "Possible options are:" << std::endl;

    for (int i = 0; i < OPTION_COUNT; i++) {
        // Construct options string
        QString opts;
        if (options[i].shortOpt)
            opts += options[i].shortOpt;
        if (options[i].longOpt) {
            if (!opts.isEmpty())
                opts += ", ";
            opts += options[i].longOpt;
        }
        if (options[i].nextToken == ntInFileName)
            opts += " <infile>";

        std::cout
            << "  " << std::left << std::setw(24) << opts.toStdString()
            << options[i].help
            << std::endl;
    }
}


bool ProgramOptions::parseCmdOptions(QStringList args)
{
    _activeOptions = 0;
    _action = acNone;
    NextToken nextToken = ntOption;
    bool found;

    // Parse the command line options
    while (!args.isEmpty()) {
        QString arg = args.front();
        args.pop_front();

        switch (nextToken)
        {
        case ntOption:
            found = false;
            for (int i = 0; i < OPTION_COUNT && !found; i++) {
                if (arg == options[i].shortOpt || arg == options[i].longOpt) {
                    found = true;
                    // Check for conflicting options
                    if (_activeOptions & options[i].conflictOptions) {
                        std::cerr
                            << "The option \"" << arg.toStdString()
                            << "\" conflicts a previously given option."
                            << std::endl;
                        return false;
                    }
                    // Is this an option?
                    if (options[i].option != opNone) {
                        _activeOptions |= options[i].option;
                    }
                    // Is this an action?
                    if (options[i].action != acNone) {
                        // Do we already have an active action?
                        if (_action != acNone) {
                            std::cerr
                                << "Only one action can be specified."
                                << std::endl;
                            return false;
                        }
                        _action = options[i].action;
                    }
                    // What do we expect next?
                    nextToken = options[i].nextToken;
                }
            }
            if (!found) {
                std::cerr << "Unknown option: " << arg.toStdString() << std::endl;
                return false;
            }
            break;

        case ntInFileName: {
            _inFileName = arg;
            QFileInfo info(_inFileName);
            if (!info.exists()) {
                std::cerr
                    << "The file or directory \"" << _inFileName.toStdString()
                    << "\" does not exist." << std::endl;
                return false;
            }
            nextToken = ntOption;
            break;
        }

        case ntMemFileName:
            if (!QFile::exists(arg)) {
                std::cerr
                    << "The file \"" << arg.toStdString()
                    << "\" does not exist." << std::endl;
                return false;
            }
            _memFileNames.append(arg);
            nextToken = ntOption;
            break;
        }
    }

    return true;
}


QString ProgramOptions::inFileName() const
{
    return _inFileName;
}


void ProgramOptions::setInFileName(QString inFileName)
{
    this->_inFileName = inFileName;
}


QStringList ProgramOptions::memFileNames() const
{
    return _memFileNames;
}


void ProgramOptions::setMemFileNames(const QStringList& memFiles)
{
    this->_memFileNames = memFiles;
}


Action ProgramOptions::action() const
{
    return _action;
}


void ProgramOptions::setAction(Action action)
{
    this->_action = action;
}


int ProgramOptions::activeOptions() const
{
    return _activeOptions;
}

