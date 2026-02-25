#pragma once
#ifndef _OP_COMMON_OSDEPENDEDMACROS__H_
#define _OP_COMMON_OSDEPENDEDMACROS__H_

#if defined (_WINDOWS) || defined(_WIN32) || defined(_WIN64)
#define OP_COMMON_OS_WINDOWS
#endif

#ifdef OP_COMMON_OS_WINDOWS
// Configure Win minimal footprint
#define WIN32_LEAN_AND_MEAN
#define  NOGDICAPMASKS     
#define    NOVIRTUALKEYCODES 
#define    NOWINMESSAGES     
#define    NOWINSTYLES       
#define    NOSYSMETRICS      
#define    NOMENUS           
#define    NOICONS           
#define    NOKEYSTATES       
#define    NOSYSCOMMANDS     
#define    NORASTEROPS       
#define    NOSHOWWINDOW      
#define    OEMRESOURCE       
#define    NOATOM            
#define    NOCLIPBOARD       
#define    NOCOLOR           
#define    NOCTLMGR          
#define    NODRAWTEXT        
#define    NOGDI             
#define    NOKERNEL          
#define    NOUSER            
#define    NONLS             
#define    NOMB              
#define    NOMEMMGR          
#define    NOMETAFILE
#ifndef NOMINMAX
#define    NOMINMAX
#endif //NOMINMAX
#define    NOMSG             
#define    NOOPENFILE        
#define    NOSCROLL          
#define    NOSERVICE         
#define    NOSOUND           
#define    NOTEXTMETRIC      
#define    NOWH              
#define    NOWINOFFSETS      
#define    NOCOMM            
#define    NOKANJI           
#define    NOHELP            
#define    NOPROFILER        
#define    NODEFERWINDOWPOS  
#define    NOMCX            

#include <Windows.h>
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#elif __linux__ //LINUX platform
    #define OP_COMMON_OS_LINUX
#elif __APPLE__
    #define OP_COMMON_OS_MACOS
#endif // _WINDOWS/__linux__/__APPLE__


#endif //_OP_COMMON_OSDEPENDEDMACROS__H_

