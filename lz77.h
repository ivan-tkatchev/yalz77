#ifndef __X_LZ77_H
#define __X_LZ77_H

/*
 * This code is in the public domain, see: http://unlicense.org/
 *
 * Feel free to steal it.
 */

/*
 * This is a variation on the LZ77 compression algorithm.
 * 
 * Highlights:
 *
 *   - Portable, self-contained, tiny implementation in readable C++. 
 *     (Header-only, no ifdefs or CPU dependencies or other stupid tricks.)
 *   - Fast decompression.
 *   - Pretty good compression
 *   - Simple 'one-button' API for realistic use-cases.
 *   - No penalty (only 2 bytes overhead) even when compressing very short strings.
 *
 * Compression performance and quality should be _roughly_ on par with LZO 
 * at high quality settings.
 * (Note that your mileage will vary depending on input data; for example, 
 * this code will degrade less gracefully compared to LZO when trying to 
 * compress uncompressable data.)
 *
 */

/* 
 * Usage:

  #include "lz77.h"

  std::string input = ...;
  std::string compressed = lz77::compress(input);

  lz77::decompress_t decompress;

  std::string temp;
  decompress.feed(compressed, temp);

  const std::string& uncompressed = decompress.result();

  --------

  Use decompress.feed(...) for feeding input data step-by-step in chunks.
  For example, if you're trying to decompress a network byte stream:

  lz77::decompress_t decompress;
  std::string extra;

  bool done = decompress.feed(buffer, extra);

  while (!done) {
    done = decompress.feed(buffer, extra);
  }

  std::string result = decompress.result();

  'feed' will start (or continue) decoding a packet of compressed data.
  If it returns true, then all of the message was decompressed.
  If it returns false, then 'feed' needs to be called with more
  compressed data until it returns 'true'.

  'extra' will hold any data that was tacked onto the buffer but wasn't
  part of this packet of compressed data. (Useful when decoding a message
  stream that doesn't have clearly delineated message boundaries; the 
  decompressor will detect message boundaries properly for you.)

  'extra' will be assigned to only when 'feed' returns true.

  'result' is the decompressed message.

  NOTE: calling feed() and result() out of order is undefined
  behaviour and will result in crashes.

*/

 

#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>

#include <string.h>
#include <stdint.h>


namespace lz77 {

// Utility function: encode a size_t as a variable-sized stream of octets with 7 bits of useful data. 
// (One bit is used to signal an end of stream.)

inline void push_vlq_uint(size_t n, std::string& out) {

    while (1) {
        unsigned char c = n & 0x7F;
        size_t q = n >> 7;

        if (q == 0) {
            out += c;
            break;
        }

        out += (c | 0x80);
        n = q;
    }
}

// Utility function: return common prefix length of two strings.

inline size_t substr_run(const unsigned char* ai, const unsigned char* ae,
                         const unsigned char* bi, const unsigned char* be) {

    size_t n = 0;

    while (1) {

        if (*ai != *bi)
            break;

        ++n;
        ++ai;
        ++bi;

        if (ai == ae || bi == be)
            break;
    }

    return n;
}

// Utility function: Hash the first 3 and 6 bytes of a string into 16-bit ints.
// (3 and 6 are magic constants.)
// The hash function itself is important for compression quality, and was arrived
// at by a series of unscientific empirical tests.

inline void pack_bytes(const unsigned char* i, uint16_t& packed3, uint16_t& packed6, size_t blocksize) {

    packed3 = ((*(i+0) << 0) | (*(i+1) << 8)) ^ (*(i+2) << 0);
    packed6 = packed3 + ((*(i+4) << 8) | (*(i+5) << 0));

    packed3 = packed3 % blocksize;
    packed6 = packed6 % blocksize;
}


// Compute the profit from compression; 'run' is the length of a string at position 'offset'.
// 'run' and 'offset' are numbers encoded as variable-length bitstreams; the sum length of 
// encoded 'run' and 'offset' must be less than 'run'.

inline size_t gains(size_t run, size_t offset) {

    size_t gain = run;
    size_t loss = 2;

    if (run > 0x7F) {
        loss++;
    }

    if (run > 0x3fff) {
        loss++;
    }

    if (run > 0x1fffff) {
        loss++;
    }

    if (offset > 0x7F) {
        loss++;
    }

    if (offset > 0x3fff) {
        loss++;
    }

    if (offset > 0x1fffff) {
        loss++;
    }

    if (loss > gain)
        return 0;

    return gain - loss;
}

// Utility function: hack a circular buffer from an std::vector.

template <typename T>
struct circular_buffer_t {

    typedef typename std::vector<T> holder_t;
    typedef typename holder_t::iterator iterator;
    typedef typename holder_t::const_iterator const_iterator;
    holder_t buff;
    iterator head;

    circular_buffer_t() : head(buff.end()) {}

    void push_back(const T& t, size_t maxsize) {

        if (buff.size() < maxsize) {
            buff.push_back(t);
            head = buff.end() - 1;

        } else {

            ++head;

            if (head == buff.end())
                head = buff.begin();

            *head = t;
        }
    }

    bool empty() {
        return (head == buff.end());
    }
    
    const_iterator begin() {
        return buff.begin();
    }

    const_iterator end() {
        return buff.end();
    }
};

// Hash table already seen strings; it maps from a hash of a string prefix to
// a list of offsets. (At each offset there is a string with a prefix that hashes
// to the key.)

struct no_hash_t {
    size_t operator()(uint16_t x) const { return x; }
};

struct offsets_dict_t {

    typedef std::unordered_map< uint16_t, circular_buffer_t<size_t>, no_hash_t > offsets_t;
    offsets_t offsets;

    size_t searchlen;

    offsets_dict_t(size_t sl, size_t bs) : searchlen(sl) { }

    void operator()(uint16_t packed, const unsigned char* i0, const unsigned char* i, const unsigned char* e,
                    size_t& maxrun, size_t& maxoffset, size_t& maxgain) {

        circular_buffer_t<size_t>& voffs = offsets[packed];

        if (!voffs.empty()) {

            circular_buffer_t<size_t>::const_iterator z = voffs.head;

            while (1) {

                int offset = i - i0 - *z;

                size_t run = substr_run(i, e, i0 + *z, e);
                size_t gain = gains(run, offset);

                if (gain > maxgain) {
                    maxrun = run;
                    maxoffset = offset;
                    maxgain = gain;
                }

                if (z == voffs.begin()) {
                    z = voffs.end() - 1;

                } else {
                    --z;
                }

                if (z == voffs.head)
                    break;
            }
        }

        voffs.push_back(i - i0, searchlen);
    }
};

/*
 * 
 * Entry point for compression.
 * 
 * Inputs: std::string of data to be compressed.
 *
 * Also optionally parameters for tuning speed and quality.
 *
 * There are two parameters: 'searchlen' and 'blocksize'.
 *
 * 'blocksize' is the upper bound for hash table sizes.
 * 'searchlen' is the upper bound for lists of offsets at each hash value.
 *
 * A larger 'searchlen' increases running time, memory consumption and compression quality. 
 * A larger 'blocksize' increases memory consumption and compression quality. (Really shouldn't be changed, the default is good.)
 *
 * Output: the compressed data as a string.
 */

inline std::string compress(const unsigned char* i, const unsigned char* e, size_t searchlen = 8, size_t blocksize = 64*1024) {

    const unsigned char* i0 = i;

    std::string ret;
        
    std::string unc;

    push_vlq_uint(e - i, ret);

    offsets_dict_t offsets3(searchlen, blocksize);
    offsets_dict_t offsets6(searchlen, blocksize);

    while (i != e) {

        unsigned char c = *i;

        // The last 5 bytes are uncompressable. (At least 6 bytes
        // are needed to calculate a prefix hash.)

        if (i > e - 6) {

            unc +=c;
            ++i;
            continue;
        }

        size_t maxrun = 0;
        size_t maxoffset = 0;
        size_t maxgain = 0;

        uint16_t packed3;
        uint16_t packed6;

        // Prefix lengths of 3 and 6 were chosen empirically, based on a series
        // of unscientific tests.

        pack_bytes(i, packed3, packed6, blocksize);

        offsets6(packed6, i0, i, e, maxrun, maxoffset, maxgain);
        offsets3(packed3, i0, i, e, maxrun, maxoffset, maxgain);

        // A substring of length less than 4 is useless for us.
        // (Theoretically a substring of length 3 can be compressed
        // to two bytes, but I found that this decreases quality in
        // practice.)

        if (maxrun < 4) {
            unc += c;
            ++i;
            continue;
        }

        
        if (unc.size() > 0) {
            // Write a packet of uncompressed data.

            size_t msg = (unc.size() << 1) | 1;
            push_vlq_uint(msg, ret);
            ret += unc;
            unc.clear();
        }

        // A compressed string is a length and an offset.
        // First subtract 3 from the length (lengths less than 3 don't exist).
        // Then check if the length fits in four bits; if it does, then
        // tack it on to the offset. Otherwise write length and offset separately.
        // The rightmost bit is a zero to differentiate from packets of
        // uncompressed data.

        i += maxrun;
        maxrun = maxrun - 3;

        if (maxrun < 16) {

            size_t msg = ((maxoffset << 4) | maxrun) << 1;
            push_vlq_uint(msg, ret);
            
        } else {

            size_t msg = (maxoffset << 5);
            push_vlq_uint(msg, ret);
            push_vlq_uint(maxrun, ret);
        }
    }

    if (unc.size() > 0) {

        size_t msg = (unc.size() << 1) | 1;
        push_vlq_uint(msg, ret);
        ret += unc;
        unc.clear();
    }

    return ret;
}

inline std::string compress(const std::string& s, size_t searchlen = 8, size_t blocksize = 64*1024) {

    const unsigned char* i = (const unsigned char*)s.data();
    const unsigned char* e = i + s.size();
    return compress(i, e, searchlen, blocksize);
}

/*
 * Entry point for decompression.
 * Calling 'feed' and 'result' out of order is undefined behaviour 
 * and will crash your program.
 */

struct decompress_t {

    std::string ret;
    unsigned char* out;
    unsigned char* outb;
    unsigned char* oute;

    struct state_t {
        size_t msg;
        size_t run;
        size_t vlq_num;
        size_t vlq_off;
        enum {
            INIT,
            START,
            READ_DATA,
            READ_RUN
        } state;

        state_t() : msg(0), run(0), vlq_num(0), vlq_off(0), state(INIT) {}
    };

    state_t state;

    // Utility function: decode variable-length-coded unsigned integers.

    bool pop_vlq_uint(const unsigned char*& i, const unsigned char* e, size_t& ret) {

        while (1) {

            if (i == e)
                return false;

            unsigned char c = *i;

            if ((c & 0x80) == 0) {
                state.vlq_num |= (c << state.vlq_off);
                break;
            }

            state.vlq_num |= ((c & 0x7F) << state.vlq_off);
            state.vlq_off += 7;
            ++i;
        }

        ret = state.vlq_num;
        state.vlq_num = 0;
        state.vlq_off = 0;

        return true;
    }


    decompress_t() : out(NULL), outb(NULL), oute(NULL) {}

    /*
     * Inputs: the compressed string, as output from 'compress()'.
     * Outputs: 
     *    true if all of the data was decompressed.
     *    false if more input data needs to be fed via 'feed()'.
     *    'remaining' will hold input data that wasn't part of
     *    the compressed message. (Only assigned to when all of
     *    the data was decompressed.)
     */

    bool feed(const std::string& s, std::string& remaining) {

        const unsigned char* i = (const unsigned char*)s.data();
        const unsigned char* e = i + s.size();

        return feed(i, e, remaining);
    }

    bool feed(const unsigned char* i, const unsigned char* e, std::string& remaining) {

        // This function is complex because it is streamable and robust.
        // The routine checks if the input isn't complete and will properly
        // pick up from where we left off when the rest of the input arrives.
        
        if (state.state == state_t::INIT) {

            ret.clear();

            size_t size;
            if (!pop_vlq_uint(i, e, size))
                return true;

            ++i;

            state = state_t();

            ret.resize(size);

            outb = (unsigned char*)ret.data();
            oute = outb + size;
            out = outb;

            state.state = state_t::START;
        }

        while (i != e) {

            if (out == oute) {
                remaining.assign(i, e);
                state.state = state_t::INIT;
                return true;
            }

            if (state.state == state_t::START) {

                if (!pop_vlq_uint(i, e, state.msg))
                    return false;

                ++i;

                state.state = ((state.msg & 1) ? state_t::READ_DATA : state_t::READ_RUN);

                state.msg = state.msg >> 1;
            }

            if (state.state == state_t::READ_DATA) {

                size_t len = state.msg;
                
                if (out + len > oute)
                    throw std::runtime_error("Malformed data while uncompressing");

                if (i == e)
                    return false;

                if (i + len > e) {

                    size_t l = e - i;
                    ::memcpy(out, &(*i), l);
                    out += l;
                    state.msg -= l;

                    return false;
                }

                ::memcpy(out, &(*i), len);
                out += len;
                i += len;

                state.state = state_t::START;

            } else if (state.state == state_t::READ_RUN) {

                size_t shortrun = state.msg & 15;

                if (shortrun) {

                    state.run = shortrun;

                } else {

                    if (!pop_vlq_uint(i, e, state.run))
                        return false;

                    ++i;
                }

                size_t off = (state.msg >> 4);
                size_t run = state.run + 3;

                unsigned char* outi = out - off;

                if (outi < outb || out + run > oute)
                    throw std::runtime_error("Malformed data while uncompressing");

                if (outi + run < out) {
                    ::memcpy(out, outi, run);
                    out += run;

                } else {

                    while (run > 0) {
                        *out = *outi;
                        ++out;
                        ++outi;
                        --run;
                    }
                }

                state.state = state_t::START;
            }
        }

        if (out == oute) {
            remaining.assign(i, e);
            state.state = state_t::INIT;
            return true;
        }

        return false;
    }

    /*
     * Returns the uncompressed result.
     */

    std::string& result() {
        return ret;
    }

};

}

#endif

