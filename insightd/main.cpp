/**
 \mainpage InSight - A Semantic Bridge for Virtual Machine Introspection and Forensic Applications
 
 \section intro Introduction
 
 InSight is a tool for <a href="https://code.google.com/p/insight-vmi/wiki/About#Background">bridging the semantic gap</a> in the field of virtual machine introspection (VMI) and digital forensics. It operates either on <a href="https://code.google.com/p/insight-vmi/wiki/InSightShell#Memory_Files">memory dump files</a> or in conjunction with any hypervisor that provides read access to the physical memory of a guest VM. InSight is written in C++ based on the Qt libraries and features <a href="https://code.google.com/p/insight-vmi/wiki/InSightShell">interactive analysis of kernel objects</a> as well as a <a href="https://code.google.com/p/insight-vmi/wiki/ScriptingEngine">JavaScript engine</a> for automation of repeating inspection tasks. The source code is licensed under the terms and conditions of the <a href="http://www.gnu.org/licenses/old-licenses/gpl-2.0.html">GNU GPL v2</a>.
 
 This tool is developed by <a href="http://www.sec.in.tum.de/christian-schneider/">Christian Schneider</a> and other contributors as part of <a href="http://www.sec.in.tum.de/leveraging-virtualization-techniques-for-system-security/">their research</a> at the <a href="http://portal.mytum.de/welcome/?set_language=en">Technische Universit&auml;t M&uuml;nchen</a>, Munich, Germany. Together with his colleges at the <a href="http://www.sec.in.tum.de/">Chair for IT Security</a>, his interest lies in the field of virtual machine introspection and how this can be used in novel ways to improve current intrusion detection methods.
 
 This documentation describes the classes and methods used by the InSight daemon. The daemon can be run in foreground or in background mode and contains all the logic and intelligence of InSight. The project is hosted at Google Code where the source code, a bug tracker, mailing lists, as well as documentation about how to setup and use InSight for kernel analysis can be found.
 
 <b>Visit the project website: <a href="https://code.google.com/p/insight-vmi/" target="_blank">https://code.google.com/p/insight-vmi/</a></b>

 \section main Main Classes
 
 For those interested in hacking the source code, the following sub-sections group the functionality into main concerns and list the relevant classes accordingly.
 
 \subsection types Type Representation
 
 InSight represents the data types of the kernel objects in various classes, all of which are derived from BaseType. They can be grouped into several sub-types:
 
 \li FloatingBaseType and IntegerBaseType are template classes that represent numeric values. They are further refined to Int8, UInt8, Int16, UInt16, Int32, UInt32, Int64, UInt64, Float, and Double.
 \li Enum represents enumerations.
 \li ConstType, Pointer, Array, Typedef, and VolatileType all represent referencing types as they refer to another type. As such, they all inherit from the virtual RefBaseType class.
 \li Struct and Union represent Structured types, they contain StructuredMember members which again reference further types.
 \li FuncPointer represents a function pointer.
 \li Variable represents a global variable at a constant location.
 
 \subsection debugsymbols Parsing Debugging Symbols
 
 The type information listed above is obtained by parsing the debugging symbols of a Linux kernel:

 \li KernelSymbolParser parses the symbols from the output generated by the \c objdump utility.
 \li KernelSymbolWriter writes the parsed symbols in a custom binary format.
 \li KernelSymbolReader reads these symbols in the custom format again.
 \li TypeInfo holds an intermediate representation of a type
 \li SymFactory creates and manages types based on data passed in a TypeInfo object.
 \li KernelSymbols is the main class for the type information. It provides the SymFactory and methods that parse, load and store the debugging symbols by utilizing the above listed classes.
 
 \subsection memdumps Accessing Memory Files
 
 To analyze the physical memory and extract kernel objects, several classes are involved:
 
 \li MemSpecs holds kernel specific constants that are required for virtual-to-physical address translation.
 \li MemSpecParser and KernelMemSpec are used to parse these constants from the debugging symbols and the kernel headers.
 \li MemoryDump represents a file of physical memory that is being analyzed.
 \li VirtualMemory performs virtual-to-physical address translation and reads data from the physical memory represented by a MemoryDump object. 
 
 \subsection shell The Shell
 
 The shell is the main interface for the user:
 
 \li Shell is the main class that executes the read-command-and-execute loop. In addition, it is responsible for setting up the scripting environment to run QtScript files.
 \li ProgramOptions holds runtime settings and parses the command line arguments.
 \li Instance objects represent an instance of a kernel object in memory. An instance must have a non-null address and reference an existing type in order to be valid.
 
 \subsection scriptengine The Scripting Engine
 
 The scripting engine is set up by the Shell.
 
 \li InstancePrototype provides all default properties and methods of an Instance object in a scripting environment. This is the probably the most interesting class if you are interested in what you can do with an Instance object within a script.
 \li InstanceClass wraps an Instance object within the scripting environment.
 
 \subsection frontend Connection between Daemon and Front-End
 
 The InSight front-end connects to the InSight daemon over a socket connection.
 
 \li Insight is the main interface class between front-end and daemon.
 \li DeviceMuxer is a helper class that multiplexes the \stdout and \stderr channel over a single socket.

 \subsection Acknowledgments

 The <a href="http://www.sec.in.tum.de/christian-schneider/" title="Christian Schneider">author</a> would like to thank the following people for their contribution to this project in form of code, in testing and tracking down bugs, or in sharing ideas and inspirations (names in alphabetic order):

 \li Cornelius Diekmann (diekmann <at> in . tum . de)
 \li Dominik Meyer (meyerd <at> in . tum . de)
 \li Hagen Fritsch (fritsch <at> in . tum . de)
 \li Jonas Pfoh (pfoh <at> in . tum . de)
 \li Sebastian Vogl (vogls <at> in . tum . de)
 \li Thomas Kittel (kittel <at> in . tum . de)

 */ 

#include <QApplication>
#include <QDir>
#include <QDateTime>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <insight/constdefs.h>

#include "debug.h"
#include "kernelsymbols.h"
#include "shell.h"
#include "genericexception.h"
#include "programoptions.h"
#include "memorymapwindow.h"


/**
 * Log a message to stderr, prepended with the current date and time.
 * @param msg the message to log
 */
void log_message(const QString& msg)
{
    std::cerr
        << QDateTime::currentDateTime().toString(Qt::SystemLocaleShortDate).toStdString()
        << " " << msg << std::endl;
}


/**
 * Signal handler for the Linux signals sent to daemon process, for more
 * signals, refer to
 * http://www.comptechdoc.org/os/linux/programming/linux_pgsignals.html
 */
void signal_handler(int sig)
{
	switch (sig) {

	case SIGHUP:
		break;

	// Terminal interrupt
    case SIGINT:
        // Try to terminate a script
        if (shell) {
            if (shell->interactive()) {
                shell->interrupt();
            }
            else {
                shell->shutdown();
                shell->wait();
            }
        }
        break;

    // Terminal quit
    case SIGQUIT:
    // Termination
    case SIGTERM:
	    if (shell) {
	        shell->shutdown();
	        shell->wait();
	    }
	    else
	        exit(0);
		break;
	}
}


/**
 * Registers signal handlers
 */
void init_signals()
{
    // Register signal handler for interesting signals
//    signal(SIGCHLD, SIG_IGN);       // ignore child
//    signal(SIGTSTP, SIG_IGN);       // ignore tty signals
//    signal(SIGTTOU, SIG_IGN);       // ignore writes from background child processes
//    signal(SIGTTIN, SIG_IGN);       // ignore reads from background child processes
    signal(SIGHUP, signal_handler);   // catch hangup signal
    signal(SIGTERM, signal_handler);  // catch kill signal
    signal(SIGINT, signal_handler);   // catch terminal interrupt
    signal(SIGQUIT, signal_handler);  // catch terminal quit
}


/**
 * Creates a background process out of the application, source code taken from:
 * http://www.enderunix.org/docs/eng/daemon.php
 * with some minor modifications
 */
void init_daemon()
{
	// Check if our parent's ID is already 1 (i.e., we are already daemonized
	if (getppid() == 1) {
		fprintf(stderr, "This process already is a child of \"init\".\n");
		return;
	}

    // Spawn a background process, if required
	if (! (programOptions.activeOptions() & opForeground) ) {
        // Create a new process
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Cannot fork this process (error no. %d), aborting.\n",
                    errno);
            exit(1);
        }
        // Parent receives child's PID and exits
        if (pid > 0)
            exit(0);

        // Child continues as daemon

        // Create a new session with this process as process group leader
        pid_t sid = setsid();
        if (sid < 0) {
            fprintf(stderr, "Cannot create a new session (error no. %d), "
                    "aborting.\n", errno);
            exit(2);
        }
	}

	// Set permissions for newly created files
	umask(027);

	QDir home = QDir::home();

	// Change running directory to home
	chdir((char*) home.absolutePath().toAscii().data());

	// Create a lock file
	QByteArray lockFile = home.absoluteFilePath(mt_lock_file).toLocal8Bit();
	int lock_fd = open(lockFile.data(), O_RDWR | O_CREAT, 0640);
	if (lock_fd < 0) {
		fprintf(stderr, "Cannot open lock file \"%s\" (error no. %d), "
				"aborting.\n", lockFile.data(), errno);
		exit(3);
	}

    // Try to lock the file
	if (lockf(lock_fd, F_TLOCK, 0) < 0) {
	    // Read in the PID
	    const int bufsize = 1024;
	    char buf[bufsize] = {0};
	    ssize_t len = read(lock_fd, buf, bufsize-1);
	    if (len < 0) {
	        fprintf(stderr, "Cannot read PID from lock file \"%s\" (error no. %d), "
	                "aborting.\n", lockFile.data(), errno);
	        exit(4);
	    }
	    for (int i = 0; i < len; ++i) {
	        if (buf[i] < '0' || buf[i] > '9') {
	            buf[i] = 0;
	            break;
	        }
	    }

		printf("Daemon already running with PID %s.\n", buf);
		exit(0);
	}
	else
        printf("Started daemon with PID %d.\n", getpid());

	// Write PID to lock file
	QByteArray myPid = QString("%1\n").arg(getpid()).toLocal8Bit();
	write(lock_fd, myPid.data(), myPid.size());

	// Detach background daemon from stdin, stdout and stderr
    if (! (programOptions.activeOptions() & opForeground) ) {
        // Close all file descriptors that the parent might have previously opened,
        // including stdin, stdout, and stderr, but exclude lock_fd
        for (int i = getdtablesize(); i >= 0; --i)
            if (i != lock_fd)
                close(i);

        // Use /dev/null for stdin and, stdout
        int fd = open("/dev/null", O_RDWR);
        dup(fd);
        // Use log file for stderr
        QByteArray logFile = home.absoluteFilePath(mt_log_file).toLocal8Bit();
        open(logFile.data(), O_CREAT|O_APPEND|O_WRONLY, 0640);
    }
}


/**
 * Entry point function that sets up the environment, parses the command line
 * options, performs the requested actions and/or spawns an interactive shell.
 */
int main(int argc, char* argv[])
{
	memMapWindow = 0;
	shell = 0;

	// Parse the command line options
    if (!programOptions.parseCmdOptions(argc, argv))
        return 1;

	int ret = 0;
    bool daemonize = (programOptions.activeOptions() & opDaemonize);

	try {
	    init_signals();

		if (daemonize) {
			init_daemon();
		    // Start a new logging session
			log_message(QString("InSight started with PID %1.").arg(getpid()));
		}

		// Delay creation of QApplication until AFTER possible fork()!
		QApplication app(argc, argv);

		if (!daemonize) {
			memMapWindow = new MemoryMapWindow();
			memMapWindow->resize(800, 600);
			memMapWindow->setAttribute(Qt::WA_QuitOnClose, false);
		}

	    shell = new Shell(daemonize);
        KernelSymbols& sym = shell->symbols();

	    // Perform any initial action that might be given
	    switch (programOptions.action()) {
	    case acNone:
	        break;
	    case acParseSymbols:
	        sym.parseSymbols(programOptions.inFileName());
	        break;
	    case acLoadSymbols:
	        sym.loadSymbols(programOptions.inFileName());
	        break;
	    case acUsage:
	        ProgramOptions::cmdOptionsUsage();
	        return 0;
	    }

        // Start the interactive shell
		shell->start();

		ret = app.exec();

		if (daemonize)
            log_message(QString("InSight exited with return code %1.").arg(ret));
		if (shell->interactive())
		    shell->out() << "Done, exiting." << endl;
	}
	catch (GenericException e) {
	    QString msg = QString("Caught exception at %1:%2\nMessage: %3")
	            .arg(e.file)
	            .arg(e.line)
	            .arg(e.message);
	    if (daemonize)
	        log_message(msg);
	    else
	        shell->err() << msg << endl;
	    return 1;
	}

	// Wait for the shell to exit
	if (shell) {
		shell->quit();
		shell->wait();
		delete shell;
	}

	if (memMapWindow)
	    delete memMapWindow;

    return ret;
}



