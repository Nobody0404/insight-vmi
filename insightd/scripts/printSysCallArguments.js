if(HAVE_GLOBAL_PARAMS){
	eval("params="+PARAMS)
	var sys_call_nr = params["sys_call_nr"] //int
	var arg0 = params["rdi"] //hex string
	var arg1 = params["rsi"] //hex string
	var arg2 = params["rdx"] //hex string
	var arg3 = params["r10"] //hex string
	var arg4 = params["r8"] //hex string
	var arg5 = params["r9"] //hex string
	var userPGD = params["cr3"] // hex string
	
	if(typeof(arg0) == "undefined") print("rdi undefined")
	if(typeof(arg1) == "undefined") print("r2i undefined")
	if(typeof(arg2) == "undefined") print("rdx undefined")
	if(typeof(arg3) == "undefined") print("r10 undefined")
	if(typeof(arg4) == "undefined") print("r8 undefined")
	if(typeof(arg5) == "undefined") print("r9 undefined")
	if(typeof(userPGD) == "undefined") print("cr3 undefined")
}else{
	print("no parameters specified")
}


/*from entry_64.S
 432 * Register setup:
 433 * rax  system call number
 434 * rdi  arg0
 435 * rcx  return address for syscall/sysret, C arg3
 436 * rsi  arg1
 437 * rdx  arg2
 438 * r10  arg3    (--> moved to rcx for C)
 439 * r8   arg4
 440 * r9   arg5
 441 * r11  eflags for syscall/sysret, temporary for C
 442 * r12-r15,rbp,rbx saved by C code, not touched.
 443 *
 444 * Interrupts are off on entry.
 445 * Only called from user space.
 446 *
 447 * XXX  if we had a free scratch register we could save the RSP into the stack frame
 448 *      and report it properly in ps. Unfortunately we haven't.
 449 *
 450 * When user can change the frames always force IRET. That is because
 451 * it deals with uncanonical addresses better. SYSRET has trouble
 452 * with them due to bugs in both AMD and Intel CPUs.
 453 */



// dict generated by gen_syscall_insight_script.py, available in diekmann's git
include("lib_syscalls_2.6.32-x64.js");
include("lib_syscalls_custom_2.6.32-x64.js");

include("lib_getCurrent_2.6.32-x64.js");

include("lib_files_2.6.32-x64.js");

include("lib_typecasting.js");

include("lib_mmap_2.6.32-x64.js");
include("lib_socket_2.6.32-x64.js");

//workaround to create an instance
//EMPTY_INSTANCE should signal that we do not care about the symbol
EMPTY_INSTANCE="init_task";


function main(){
	var tmpInst = new Instance(EMPTY_INSTANCE)
	tmpInst.ChangeType("uint64_t");
	tmpInst.SetAddress("0");
	
	current = getCurrentTask(GS_BASE_2632x64);
	
	print(SysCalls[sys_call_nr]["sys_name"]+": "+SysCalls[sys_call_nr]["signature"]);
	
	var line = "";
	
	// set this to true if we have strong indication that a dump of the current memory
	// mappings would be usefull to display
	var want_mmap_mappings = false;
	
	if(SysCalls[sys_call_nr]["sys_name"] == "sys_mmap" || 
		SysCalls[sys_call_nr]["sys_name"] == "sys_munmap" || 
		SysCalls[sys_call_nr]["sys_name"] ==  "sys_mremap" || 
		SysCalls[sys_call_nr]["sys_name"] ==  "sys_remap_file_pages") want_mmap_mappings = true;
	
	if(SysCalls[sys_call_nr]["signature"] == None){
		line += "unknown signature, dumping registers\n"
		for(i=0; i < 6; i++){
			var value = eval("arg"+i)
			line += "arg"+i+": 0x"+value+"\n";
		}
		print(line)
	}
	
	var i;
	
	var privateData; // variable used by human readable representation function to store data which could be userfull for the next argument to print

	for(i=0; i < SysCalls[sys_call_nr]["argc"]; i++){
		var arg = SysCalls[sys_call_nr]["arg"+i];
		var next_arg = SysCalls[sys_call_nr]["arg"+(i+1)];
	
		// global parameter sys_call_arg is now the conetent of the register which contains the syscall parameter
		var sys_call_arg = eval("arg"+i) ;
	
		line = "\t";
	
		var arg_name = arg["name"] == "" ? "?unknown?" : arg["name"];
		line += arg_name+": ";
		
		tmpInstValid = false;
		if(!arg["isPtr"]){
			// treat argument as value in register, try to cast to specific type
			// and print it
			line += arg["type"]+": ";
			line += "0x"+sys_call_arg+" ";
				
			tmpInst.SetAddress("0"); // ignore addr
			try{
				__tryChangeType(tmpInst, arg["type"]);
				tmpInstValid = true;
			}catch(e){
				line += "exception: ";
				line += e;
			}

			if(tmpInstValid){
				
				//try to provide some human readable representation
				if(SysCalls[sys_call_nr]["sys_name"] == "sys_mmap"){
					if(arg["name"] == "flags"){
						// dereference mmap flags
						var flags = getMMAPsyscall_flags(parseInt(sys_call_arg, 16));
						line += " -> "+flags;
						if(flags.indexOf("MAP_ANONYMOUS") != -1){
							privateData = "MAP_ANONYMOUS";
						}else{
							privateDATA = "";
						}
					}else if(privateData == "MAP_ANONYMOUS" && (arg["name"] == "fd" || arg["name"] == "pgoff")){
						line += " (ignored due to MAP_ANONYMOUS)";
					}
				}
				
				if(SysCalls[sys_call_nr]["sys_name"] == "sys_socket"){
					line += __getSocketParams(arg["name"], parseInt(sys_call_arg, 16));
				}
				
				if(arg["name"] == "fd" || arg["name"] == "sockfd"){
					// print fd
					var fd = parseInt(sys_call_arg, 16);
					line += " -> ";
					
					var files_struct = current.files;
					//print(current)
					
					try{
						//TODO InSight bug, must change type here
						files_struct.ChangeType("files_struct");
						//print(files_struct + " \n@ " + files_struct.Address());
						//print(files_struct.TypeName()) // struct files_struct
					
						var fdtable = files_struct.fdt;
						//file = fdtable.fd[fd]
						var file = fdtable.fd;
						
						
						//print(fdtable.Address())
						//print("fdtable:"+fdtable)
						fdtable.ChangeType("uint64_t");
						//print(fdtable.Address())
						fdtable.AddToAddress(8);
						//print("file ptr for given fd at: " + fdtable.Address().toString());
						
						//print("1##########################################################################################################################################")
						
						var file_ptr = fdtable;
						//file_ptr.ChangeType("uint64_t");
						file_ptr.SetAddress(__uintToHex(fdtable));
						file_ptr.AddToAddress(fd*POINTER_SIZE);
						
						file_ptr = __uintToHex(file_ptr);
						//print("file for given fd at "+file_ptr);
						
						file.SetAddress(file_ptr);
						//print("2##########################################################################################################################################")
						
						//howto deetermine wether it is a socket:
						// static struct socket *sock_from_file(struct file *file, int *err)
						// 418{
						// 419        if (file->f_op == &socket_file_ops)
						// 420                return file->private_data;      /* set in sock_map_fd */
						// 421
						// 422        *err = -ENOTSOCK;
						// 423        return NULL;
						// 424}
						// --socket
						if(__fileIsSocket(file)){
						
							line += "(socket) -> ";
							var private_data = file.private_data;
							var socket = new Instance(EMPTY_INSTANCE);
							
							private_data.ChangeType("uint64_t");
							socket.SetAddress(__uintToHex(private_data));
							
							socket.ChangeType("socket");
							//print(socket.Address())
							if(socket.Size() == 0){
								line += "type "+socket.TypeName()+" currently unknown to InSight; "
							}
							//print(socket.Address());
							
							socket.AddToAddress(4);
							line += "type: " + __getSocketParams("type", socket.toUInt16());
							socket.AddToAddress(2);
							line += " flags: " + __getSocketParams("flags", socket.toUInt64());
							//socket.AddToAddress(32);
							//socket.ChangeType("uint64_t")
							//
							//var sock = new Instance(EMPTY_INSTANCE);
							//var addr = __uintToHex(socket);
							//print(addr);
							//sock.SetAddress(addr);
							//sock.ChangeType("sock");
							//print(sock)
							
						}else{
						
							// -- end socket

						
							//print(fdtable.fd.Address());
							//print(file.Address());
							//print(file)
					
							//TODO InSight bug
							//without ChangeType (type is already file" an error occurs
							//file.ChangeType("file");
							var dentry = file.f_path.dentry;
							//print(dentry.toString());
							if(dentry.toString() != "NULL"){
								//print(file.f_path.Address())
								//print(file.f_path);
						
								var tmp = dentry.d_iname
								tmp.ChangeType("char");
								//TODO address of d_iname calculated by hand!!
								var addr = __hex_add(dentry.Address(), "a0");
								tmp.SetAddress(addr);
				
								var filePath = __dumpStringKernel(tmp, 32);
								line += "\"" + filePath + "\"";
							}else{
								line += "no filename";
						}
						}
					}catch(e){
						line += e;
					}
						
				} 
				
			}
		}else{
			// treat argument as pointer, just print value
			line += arg["type"]+" "+arg["user_ptr"]+": 0x"+sys_call_arg;
			
			//try to deref the value
			try{
				__tryChangeType(tmpInst, arg["type"]);
				tmpInst.SetAddress(sys_call_arg);
				tmpInstValid = true;
			}catch(e){
				line += " cannot dereference: ";
				line += e;
			}

			if(tmpInstValid){
				try{
				
					var dereferenced = tmpInst.toStringUserLand(userPGD);
					
					line += " -> \n\t\t"+dereferenced.replace(/\n/g, "\n\t\t");
					
					// now we try to output some nice humean readable representation if some arguments
					// are obvious
					if(arg["name"] == "filename"){
						// derefernece filenames
						line += " hex: ";
						var str_filename = "";
						for(var k = 0; k < 256; k++){
							var thisChar = parseInt(tmpInst.toStringUserLand(userPGD), 10)
							if(thisChar == 0) break;
							line += thisChar.toString(16)+" ";
							str_filename += String.fromCharCode(thisChar);
							tmpInst.AddToAddress(1);
						}
						line += "string: "+str_filename
					}else if(arg["name"] == "buf" && (next_arg["name"] == "count" || next_arg["name"] == "len") && SysCalls[sys_call_nr]["sys_name"] != "sys_read"){
						// dereference buffers
						var buffSize = parseInt(eval("arg"+(i+1)), 16);
						line += "\nbuffer content hex (of size "+buffSize+"):\n";
						var str_buffer = "";
						for(var k = 0; k < buffSize; k++){
							var thisChar = parseInt(tmpInst.toStringUserLand(userPGD), 10);
							line += thisChar.toString(16)+" ";
							str_buffer += String.fromCharCode(thisChar);
							tmpInst.AddToAddress(1);
						}
						line += "\nbuffer content string: " + str_buffer
					}else if(arg["name"] == "argv" || arg["name"] == "envp"){
						//dereference command line
						//array, nullterminated of pointers to strings
						
						if(tmpInst.Size() != 1) throw("char not of size 1!!");
						
						var strInst = new Instance("init_task").comm; // type char array to automatically print string
						strInst.SetAddress("42"); // something not NULL
						
						for(var j=0; !strInst.IsNull(); ++j){
							//TODO assuming pointer is 8 byte long
							var addr = ""; // address of next char *
							for(var k = 0; k < 8; ++k){
								var thisChar = parseInt(tmpInst.toStringUserLand(userPGD), 10);
								if(thisChar < 0) thisChar = 256 + thisChar;
								thisChar = thisChar.toString(16);
								if(thisChar.length == 1) thisChar = "0"+thisChar;
								addr = thisChar + addr; //little endian
								tmpInst.AddToAddress(1);
							}
							line += "\n\t\t\t" + arg["name"] + "["+ j +"]" + " @ " + "0x"+addr;
						
							strInst.SetAddress(addr);
							line += " -> "+strInst.toStringUserLand(userPGD);
						}
						
					}else if(arg["type"] == "struct sockaddr"){
						//line += " -> -------------------------------------------------------------------------------------------------" ;
						var tmpLenStr =eval("arg"+(i+1)).toString();// next arg
						var tmpLen = parseInt(tmpLenStr, 16); 
						line += " -> " + __getSockaddr(tmpInst, tmpLen, userPGD);
					}else if(arg["type"] == "struct msghdr"){
						//line += " -> -------------------------------------------------------------------------------------------------" ;
						line += __printSocketMsgHdr(tmpInst, userPGD);
					}
				}catch(e){
					line += " cannot dereference: ";
					line += e;
					
					want_mmap_mappings = true; //if this address was mmapped right before, the kernel 
					// performs the mapping lazy on first access to this region, so we want to see
					// at least the current mappings to check whether this access failed due to
					// lazy mapping
				}
				
			}
		}
		print(line)
	
	}
	if(want_mmap_mappings){
		line = "Memory mapping before syscall:\n";
		var mmap = getMemoryMap(current)
		line += mmap;
		print(line)
	}
	
}

try{
	main()
}catch(e){
	print("Exception in main():")
	print(e)
}
