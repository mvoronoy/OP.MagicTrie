#pragma once
#ifndef _OP_EXCLUSIVESTREAM__H_
#define _OP_EXCLUSIVESTREAM__H_

#include <fstream>
#include <cstdio>
#include <filesystem>

namespace OP
{
    /** \brief Open std::ofstream in exclusive mode 
    *
    * Special thanks and credits to https://stackoverflow.com/a/75017250/149818
    */
    template <class TStream = std::ofstream>
    TStream exclusive_open(const char* file_path, std::ios_base::openmode mode = std::ios_base::out)
    {
        const auto deleter = [](FILE*file){ std::fclose(file); };
        std::unique_ptr<std::FILE, decltype(deleter)> fp(std::fopen(file_path, "wx"), deleter);
        auto saveerr = errno;

        std::ofstream stream;
    
        if (!fp) 
        {
          stream.setstate(std::ios::failbit);
          errno = saveerr;
        } 
        else 
        {
            stream.open(file_path, mode);
        } 
        return stream;    
    }

} //end of namespace OP

#endif //_OP_EXCLUSIVESTREAM__H_
