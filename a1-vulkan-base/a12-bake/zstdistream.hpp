#ifndef ZSTDISTREAM_HPP_5A949FD3_ADF5_4751_A1F8_6E0283D540D1
#define ZSTDISTREAM_HPP_5A949FD3_ADF5_4751_A1F8_6E0283D540D1

#include <istream>
#include <memory>

// Rapidobj fortunately allows us to feed it a custom data stream. Somewhat
// unfortunately, the interface for this is a std::istream.
//
// Hence, to to decompress stuff on the fly, we have to write a std::istream /
// std::streambuf adaptor. :-(

class ZStdIStream : public std::istream
{
	public:
		explicit ZStdIStream( char const* aPath );

	private:
		std::unique_ptr<std::streambuf> mInternal;
};

#endif // ZSTDISTREAM_HPP_5A949FD3_ADF5_4751_A1F8_6E0283D540D1
