#ifndef CHAR_STREAM_H
#define CHAR_STREAM_H

#include <Arduino.h>

/**
 * Influenced by GString helper class presented here: http://forum.arduino.cc/index.php?topic=166540.0
 */
template<int n>
class CharStream : public Print {
    char buffer[n] = {0};
  public:

    /**
     * Default constructor, does nothing special
     */
    CharStream () : position (buffer) {}

    /**
     * cast operator char* is used to enable ponter arithmetics and direct access to characters via [] operator
     */
    operator char* () {
        *position = 0;
        return buffer;
    }

    /**
     * cast operator const char* is used to enable ponter arithmetics and direct reading of characters via [] operator
     */
    operator const char* () {
        *position = 0;
        return buffer;
    }

    /**
     * cast operator const char* is used to enable ponter arithmetics and direct reading of characters via [] operator
     */
    const char* str() {
        *position = 0;
        return buffer;
    }

    /**
     * A little helper function which resets the buffer position to 0 and returns the stream itself.
     * Intended use:
     *      CharStream<20> myStream;
     *      myStream.start() << "bla bla";
     *      // do something with myStream, then reuse the stream to hold another string
     *      myStream.start() << "another string>"
     * @return itself (to be used as the target for << operator)
     */
    CharStream& start() {
        clear();
        return *this;
    }

    /**
     * Reset the string position back to zero; optionally also clear the stream; in any case first char is set to 0
     * @param doClear clear the whole stream with zeros
     */
    void clear (bool doClear = false) {
        if (doClear)
            while ( --position > buffer )
                *position = 0;
        position = buffer;
        buffer[0] = 0;
    }

    /**
     * Standard concatenate function
     */
    template<typename T>
    CharStream& concat ( const T& t ) {
        print ( t );
        return *this;
    }

    /**
     * Concatenate with a parameter that defines the form of the concatenated object
     */
    template<typename T>
    CharStream& concat (const T& t, const int i) {
        print (t, i);
        return *this;
    }

    /**
     * Concatenation operator
     */
    template<typename T>
    CharStream& operator += ( const T& t ) {
        return concat(t);
    }

    /**
     * Count the number of used characters
     * @return he number of used characters
     */
    size_t count () const {
        return position - buffer;
    }

    /**
     * Get the buffer free space size [in characters]
     * @return number of characters available
     */
    size_t getNumFree () const {
        return n-count();
    }

    /**
     * Rewind (or maybe fast forward) the buffer to one byte after its last character, making it invalide to write to
     */
    void rewindToEnd() {
        position = buffer + n;
    }

    /**
     * Add a terminating null to the stream
     */
    void end() {
        *position = 0;
        ++position;
    }

    /**
     * Find the character within the stream (only the used part), return the first occurance.
     * @param  target the target character
     * @return        index of the first character occurance within the stream or -1 f not found
     */
    size_t find(const char target) {
        char* cursor = buffer;
        while ( ( *cursor != target ) && ( cursor < position ) )
            ++cursor;
        return cursor == position ? -1 : cursor - buffer; //Nothing found if reached position.
    }

    /**
     * Fill the stream with repeats of the given character; Warning: no range checking is performed.
     * @param c      [description]
     * @param count  [description]
     */
    void fill(const char c, size_t count) {
        for (; count>0; count--)
            position[count] = c;
        position += count;
    }

    /**
     * Convert (inline) the stream into lower case characters
     */
    void toLower() {
        translate('A', 'Z', 'a');
    }

    /**
     * Convert (inline) the stream to upper case characters
     */
    void toUpper() {
        translate('a', 'z', 'A');
    }

    /**
     * Convert a range of characters to another range of characters
     * @param srcLow  the first character in source range
     * @param srcHigh the last character in source range
     * @param destLow the first character in destination range
     */
    void translate(const char srcLow, const char srcHigh, const char destLow) {
        char* cursor = buffer;
        while (cursor < position) {
            if ( (srcLow <= *cursor) && (*cursor <= srcHigh) ) {
                *cursor = destLow + (*cursor - srcLow);
            }
            ++cursor;
        }
    }

  protected:
    /**
     * Implementation of the Print interface: write byte buffer function.
     * Bytes are passed in and written into buffer at current position. No range checking is performed.
     * @param  buffer input buffer
     * @param  size   input buffer size
     * @return        number of written bytes
     */
    size_t write(const uint8_t* buffer, size_t size) {
        const size_t ret = size;
        while (size--)
            *position++ = *buffer++;
        return ret;
    }

    /**
     * Implementation of the Print interface: write single byte function.
     * Byte is written into buffer at current position. No range checking is performed.
     * @param  data the input data
     * @return      always 1, as in 1 byte written / true
     */
    size_t write(uint8_t data) {
        *position++ = data;
        return 1;
    }

  private:
    char* position;
};

#endif //CHAR_STREAM_H
