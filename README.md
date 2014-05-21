yalz77
======

Yet another LZ77 implementation.

This code is in the public domain, see: http://unlicense.org/

Feel free to steal it.

----

This is a variation on the LZ77 compression algorithm.
 
Highlights:

- Portable, self-contained, tiny implementation in readable C++. 
  (Header-only, no ifdefs or CPU dependencies or other stupid tricks.)
- Fast decompression.
- Pretty good compression quality.
- Simple 'one-button' API for realistic use-cases.
- No penalty (only 3 bytes) even when compressing very short strings.

Compression performance and quality should be _roughly_ on par with LZO at highest quality setting.
(Note that your mileage will vary depending on input data; for example, this code will degrade less
gracefully compared to LZO when trying to compress uncompressable data.)

Usage:

    #include "lz77.h"
    
    std::string input = ...;
    std::string compressed = lz77::compress(input);
    
    lz77::decompress_t decompress;
    
    std::string temp;
    decompress.feed(compressed, temp);
    
    const std::string& uncompressed = decompress.result();

Use `decompress.feed(...)` for feeding input data step-by-step in chunks.
For example, if you're trying to decompress a network byte stream:

    lz77::decompress_t decompress;
    std::string extra;
    
    bool done = decompress.feed(buffer, extra);
    
    while (!done) {
      buffer = ...
      done = decompress.feed(buffer, extra);
    }
    
    std::string result = decompress.result();

`feed()` will start (or continue) decoding a packet of compressed data.
If it returns true, then all of the message was decompressed.
If it returns false, then `feed()` needs to be called with more
compressed data until it returns `true`.

`extra` will hold any data that was tacked onto the buffer but wasn't
part of this packet of compressed data. (Useful when decoding a message
stream that doesn't have clearly delineated message boundaries; the 
decompressor will detect message boundaries properly for you.)

`extra` will be assigned to only when `feed()` returns true.

`result` is the decompressed message.

NOTE: calling `feed()` and `result()` out of order is undefined
behaviour and will result in crashes.

