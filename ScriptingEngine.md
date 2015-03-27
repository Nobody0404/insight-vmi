# The Scripting Engine #



# Introduction #

The scripting engine provides a powerful and intuitive way to inspect kernel objects and follow structure members to further objects. It is implemented using the [QtScript module](http://developer.qt.nokia.com/doc/qt-4.7/qtscript.html) of the Qt libraries. Both JavaScript and QtScript are based on the !ECMAScript scripting language, so anybody familiar with JavaScript will instantly feel at home with QtScript.

# Scripting Basics #

The scripts that can be written in InSight support all common language features also found in JavaScript. In addition to that, some new functions and objects have been added to allow analysis of kernel objects. A complete list of these additions is available in the [function reference](#Function_Reference.md).

## The `Instance` Object ##

All kernel objects in the scripting environment are represented as `Instance` objects. To obtain a kernel object based on a global variable, just pass the name or the ID of that variable to the constructor of the `Instance` object (the ID might be less portable, though):

```js

var swapper = new Instance("init_task");```

The constructor also takes queries in the form of `object.member` in the same way [as the shell does](InSightShell#Memory_Analysis.md):

```js

var init = new Instance("init_task.tasks.next");```

The `Instance` objects provide various functions to inspect the properties of the kernel object, such as its type, its virtual address, its size, etc. Again, refer to the [function reference](#Function_Reference.md) for a complete list of methods that an `Instance` object provides.

```js

// Retrieve information about the kernel object
print("init.Address() = 0x" + init.Address());
print("init.FullName() = " + init.FullName());
print("init.Type() = " + init.Type());
print("init.TypeName() = " + init.TypeName());
print("init.Size() = " + init.Size());```

## Accessing Members ##

If an `Instance` represents a structure or a union, its members can be accessed using the `Members()` method. This method returns an array of `Instance` objects of all members of the callee:

```js

// Iterate over all members using an array of Instance objects
var members = init.Members();
for (var i = 0; i < members.length; ++i) {
var offset = init.MemberOffset(members[i].Name());
print ((i+1) +
". 0x" + offset.toString(16) + " " +
members[i].Name() + ": " +
members[i].TypeName() +
" @ 0x" + members[i].Address());
}```

The members can also be accessed using a JavaScript-style iterator:

```js

// Iterate over all members using an iterator
var i = 1;
for (m in init) {
var offset = init.!MemberOffset(init[m].Name());
print ((i++) +
". 0x" + offset.toString(16) + " " +
init[m].Name() + ": " +
init[m].!TypeName() +
" @ 0x" + init[m].Address());
}```

If the name of a member is known, it can be directly accessed as a property of that `Instance` object:

```js

// Print a list of running processes
var proc = swapper;
do {
print(proc.pid + "  " + proc.comm);
proc = proc.tasks.next;
} while (proc.Address() != swapper.Address());```

**Note:** In order to prevent name collisions between member names of a kernel objects and the methods provided by the `Instance` object itself, all `Instance` methods start with an upper-case letter, as the previous examples showed for `Address()`, `Size()`, or `Members()`. The only exception to this rule are the explicit casting methods, such as `toString()`, `toInt32()`, `toFloat()` etc.

## Running a Script ##

Scripts are executed using the `script` command within the InSight shell, as explained in detail on the [InSightShell](InSightShell#Executing_Scripts.md) page. Save the following example as `hello.js`:

```js

// contents of file "hello.js"
print("Hello, world!");```

The output of the script is written to the shell:

```
>>> script scripts/hello.js
Hello, world!
```

## Arguments to Scripts ##

The script file name can optionally be followed by one argument that is passed to the script. Access the parameter through the array `ARGV`:

```js

// contents of file "hello.js"
print("Hello, my arguments are:");
for (var i = 0; i < ARGV.length; i++)
print ("[" + i + "] " + ARGV[i]);```

Note that the value at array index 0 holds the name of the script being executed. The first user-supplied argument is found at array index 1:

```
>>> sc scripts/hello.js foo bar "with spaces"
Hello, my arguments are:
[0] scripts/hello.js
[1] foo
[2] bar
[3] with spaces
```

## Using Multiple Memory Files ##

The list of loaded memory files can be accessed within a script using the `getMemDumps()` command. The following example prints out the list:

```js

// Obtain the list of loaded memory files
var dumps = getMemDumps();

if (dumps.length <= 0)
print("No memory dumps loaded.");
else {
for (var i in dumps)
print("[" + i + "] " + dumps[i]);
}```

To request a kernel object from a specific memory dump, the constructor of an `Instance` objects accepts the index of the memory file as an optional second parameter. If this parameter is omitted, the first memory file in the list will be used:

```js

// Explicitly request kernel object from first file
var swapper1 = new Instance("init_task", 0);

// Explicitly request kernel object from second file
var swapper2 = new Instance("init_task", 1);

// Implicitly request kernel object from first file
var swapper3 = new Instance("init_task");```

All members of an `Instance` object will be read from the same memory file.

## Including Other Scripts ##

It is advisable to split scripts up into several files and encapsulate pieces of logic into functions. Using the `include()` function within a script, other files can be included. That way it is easy to build up a library of analysis functions that can be re-used in multiple scripts:

```js

// Include custom library file
include("my_library.js");

// Now use functions or objects from that library
// ...```

# Function Reference #

The ECMA-262 specification that QtScript implements already defines various classes, methods and functions. A complete list of these can be found in the [QtScript documentation](http://developer.qt.nokia.com/doc/qt-4.7/ecmascript.html).

For all the methods that an `Instance` object provides to interact with a kernel object, refer to the [InstancePrototype class documentation](http://www.sec.in.tum.de/~chrschn/insightd-doc/classInstancePrototype.html).

In addition to that, the following global functions, properties and constructors are available:

| **Name**               | **Type**   | **Description** |
|:-----------------------|:-----------|:----------------|
| `Instance([`_name_`|`_id_` [, `_index_`]])` | constructor | Creates an `Instance` object for variable _name_ or _id_, reading its values from memory file _index_. If _index_ is omitted, the first memory file is used. If neither _name_ nor _id_ is specified, an empty `Instance` object is created. |
| `getMemDumps()`      | function | Returns an array of all loaded memory files. |
| `getVariableIds()`   | function | Returns an array containing the IDs of all global variables. |
| `getVariableNames()` | function | Returns an array containing the names of all global variables. |
| `include(`_file_`)`  | function | Executes _file_ in the current context of the script. |
| `ARGV`               | property | Array holding the script's file name as well as the arguments supplied to the script |
| `SCRIPT_PATH`        | property | Holds the path to the script that is being executed. |