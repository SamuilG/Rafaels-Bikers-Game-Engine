#ifndef ANGLE_HPP_35E48A99_447E_4773_B502_43EE3C436CFC
#define ANGLE_HPP_35E48A99_447E_4773_B502_43EE3C436CFC
// SOLUTION_TAGS: vulkan-(ex-[^123]|cw-.)

#include <numbers>

namespace labut2
{
	template< typename tScalar >
	constexpr tScalar deg_to_rad( tScalar aDegrees )
	{
		return aDegrees * std::numbers::pi_v<tScalar> / tScalar(180);
	}
	template< typename tScalar >
	constexpr tScalar rad_to_deg( tScalar aRadians )
	{
		return aRadians * tScalar(180) * std::numbers::inv_pi_v<tScalar>;
	}


	template< typename tScalar > class Degrees;
	template< typename tScalar > class Radians;

	template< typename tScalar >
	class Degrees
	{
		public:
			constexpr explicit Degrees( tScalar aValue = tScalar(0) ) noexcept
				: mValue( aValue )
			{}

			constexpr Degrees( Degrees const& aOther ) noexcept
				: mValue( aOther.mValue )
			{}
			constexpr Degrees& operator= (Degrees const& aOther) noexcept
			{
				mValue = aOther.mValue;
				return *this;
			}

			constexpr Degrees( Radians<tScalar> const& aRadians ) noexcept
				: mValue( rad_to_deg( aRadians.value() ) )
			{}
			constexpr Degrees& operator= (Radians<tScalar> const& aRadians ) noexcept
			{
				mValue = rad_to_deg( aRadians.value() );
				return *this;
			}

		public:
			constexpr tScalar value() const noexcept
			{
				return mValue;
			}

		private:
			tScalar mValue;
	};

	template< typename tScalar >
	class Radians
	{
		public:
			constexpr explicit Radians( tScalar aValue = tScalar(0) ) noexcept
				: mValue( aValue )
			{}

			constexpr Radians( Radians const& aOther ) noexcept
				: mValue( aOther.mValue )
			{}
			constexpr Radians& operator= (Radians const& aOther) noexcept
			{
				mValue = aOther.mValue;
				return *this;
			}

			constexpr Radians( Degrees<tScalar> const& aDegrees ) noexcept
				: mValue( deg_to_rad( aDegrees.value() ) )
			{}
			constexpr Radians& operator= (Degrees<tScalar> const& aDegrees ) noexcept
			{
				mValue = deg( aDegrees.value() );
				return *this;
			}

		public:
			constexpr tScalar value() const noexcept
			{
				return mValue;
			}

		private:
			tScalar mValue;
	};


	using Degreesf = Degrees<float>;
	using Radiansf = Radians<float>;

	template< typename tScalar > constexpr 
	tScalar to_degrees( Degrees<tScalar> aValue ) noexcept
	{
		return aValue.value();
	}
	template< typename tScalar > constexpr 
	tScalar to_radians( Radians<tScalar> aValue ) noexcept
	{
		return aValue.value();
	}


	inline namespace literals
	{
		constexpr 
		auto operator "" _radf( long double aValue ) noexcept
		{
			return Radiansf( float(aValue) );
		}
		constexpr 
		auto operator "" _degf( long double aValue ) noexcept
		{
			return Degreesf( float(aValue) );
		}
	}
}

#endif //ANGLE_HPP_35E48A99_447E_4773_B502_43EE3C436CFC
