#include <iostream>
#include "lz77.h"

#include <stdio.h>


int main(int argc, char** argv) {

    if (argc != 2) {
        fprintf(stderr, "Usage: %s [-c|-d], where -c is compression and -d is decompression.\n"
                "Input is stdin and and output is stdout.\n", argv[0]);
        return 1;
    }

    std::string mode(argv[1]);
    
    if (mode == "-c") {

        std::string buff;
        buff.resize(10*1024*1024);

        while (1) {
            size_t i = ::fread((void*)buff.data(), 1, buff.size(), stdin);

            if (i > 0) {
                std::string out = lz77::compress(buff);
                ::fwrite(out.data(), 1, out.size(), stdout);
            }

            if (i != buff.size())
                break;
        }
    
    } else if (mode == "-d") {

        std::string buff;
        buff.resize(10*1024*1024);

        lz77::decompress_t decompress;
        std::string extra;

        while (1) {
            size_t i = ::fread((void*)buff.data(), 1, buff.size(), stdin);

            if (i == 0)
                break;

            std::string* what = &buff;
            
            while (1) {

                bool done = decompress.feed(*what, extra);

                if (!done)
                    break;

                std::string result = decompress.result();
                ::fwrite(result.data(), 1, result.size(), stdout);

                what = &extra;
            }

            if (i != buff.size())
                break;
        }

    } else {
        fprintf(stderr, "Usage: %s [-c|-d], where -c is compression and -d is decompression.\n"
                "Input is stdin and and output is stdout.\n", argv[0]);
        return 1;
    }

    return 0;
}
