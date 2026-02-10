#include "zstdistream.hpp"

#include <streambuf>
#include <fstream>

#include <zstd.h>

#include "../labut2/error.hpp"
namespace lut = labut2;

namespace
{
	class ZStdStreambuf_ : public std::streambuf
	{
		public:
			ZStdStreambuf_( char const* aPath ), ~ZStdStreambuf_();

		protected:
			int underflow() override;

		private:
			char* mOutBuf;
			std::size_t mOutSize;

			char* mInBuf;
			std::size_t mInSize;
			ZSTD_inBuffer mInState;

			ZSTD_DCtx* mCtx;

			std::ifstream mStream;
	};
}

ZStdIStream::ZStdIStream( char const* aPath )
	: std::istream( nullptr )
	, mInternal( std::make_unique<ZStdStreambuf_>(aPath) )
{
	rdbuf( mInternal.get() );
}

namespace
{
	ZStdStreambuf_::ZStdStreambuf_( char const* aPath )
		: mStream( aPath, std::ios::binary )
	{
		if( !mStream.is_open() )
			throw lut::Error( "Unable to open '{}'", aPath );

		// Init ZStd
		mOutSize = ZSTD_DStreamOutSize();
		mOutBuf = reinterpret_cast<char*>(::operator new( mOutSize ));

		mInSize = ZSTD_DStreamInSize();
		mInBuf = reinterpret_cast<char*>(::operator new( mInSize ));

		mCtx = ZSTD_createDCtx();
		if( !mCtx )
			throw lut::Error( "ZSTD_createDCtx(): returned error" );

		// Fill buffer once
		mStream.read( mInBuf, mInSize );
		if( mStream.bad() )
			throw lut::Error( "Reading: badness happened" ); // :-(

		mInState.src = mInBuf;
		mInState.pos = 0;
		mInState.size = mStream.gcount(); // iostreams are terrible.

		// Decompress once
		ZSTD_outBuffer ob{ mOutBuf, mOutSize, 0 };
		auto const ret = ZSTD_decompressStream( mCtx, &ob, &mInState );
		if( ZSTD_isError(ret) )
			throw lut::Error( "Decompression: {}", ZSTD_getErrorName(ret) );

		// Initialize stream buffer
		setg( mOutBuf, mOutBuf, mOutBuf + ob.pos );
	}

	ZStdStreambuf_::~ZStdStreambuf_()
	{
		::operator delete( mInBuf );
		::operator delete( mOutBuf );

		ZSTD_freeDCtx( mCtx );
	}

	int ZStdStreambuf_::underflow()
	{
		// Decompressed buffer empty?
		if( gptr() == egptr() )
		{
			// Input buffer empty?
			if( mInState.pos == mInSize )
			{
				mStream.read( mInBuf, mInSize );
				if( mStream.bad() )
					throw lut::Error( "Reading: badness happened" ); // :-(

				mInState.pos = 0;
				mInState.size = mStream.gcount();
			}

			ZSTD_outBuffer ob{ mOutBuf, mOutSize, 0 };
			auto const ret = ZSTD_decompressStream( mCtx, &ob, &mInState );
			if( ZSTD_isError(ret) )
				throw lut::Error( "Decompression: {}", ZSTD_getErrorName(ret) );

			setg( mOutBuf, mOutBuf, mOutBuf + ob.pos );
		}

		return gptr() == egptr() 
			? traits_type::eof() 
			: traits_type::to_int_type( *gptr() )
		;
	}
}
