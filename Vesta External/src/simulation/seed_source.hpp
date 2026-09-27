#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <core/math/vector.hpp>
#include <core/math/random.hpp>

// Adapted from SeedTrigger-Source's seed_trigger.cpp. Memory discovery,
// damage/visibility policy and input lifetime remain owned by Vesta.
namespace simulation::seed_source {

inline float normalize_angle( float angle ) noexcept
{
	angle = std::fmod( angle, 360.0f );
	if ( angle > 180.0f ) angle -= 360.0f;
	if ( angle < -180.0f ) angle += 360.0f;
	return angle;
}

inline float quantize_angle( float angle ) noexcept
{
	return std::floor( normalize_angle( angle ) * 2.0f ) * 0.5f;
}

inline bool bucket_is_safe( float angle, float guard = 0.01f ) noexcept
{
	if ( !std::isfinite( angle ) || !std::isfinite( guard ) ) return false;
	if ( guard <= 0.0f ) return true;
	const auto normalized = normalize_angle( angle );
	const auto inside = normalized - quantize_angle( normalized );
	return std::min( inside, 0.5f - inside ) >= guard;
}

inline bool safe_angles( const foundation::vec3& angles ) noexcept
{
	return std::isfinite( angles.z ) && bucket_is_safe( angles.x )
		&& bucket_is_safe( angles.y );
}

struct shot_angles
{
	foundation::vec3 hash;
	foundation::vec3 direction;
};

inline std::optional<shot_angles> prepare_angles( bool host,
	const foundation::vec3& view, const foundation::vec3& pawn,
	const foundation::vec3& punch ) noexcept
{
	const auto valid = []( const foundation::vec3& value ) {
		return std::isfinite( value.x ) && std::isfinite( value.y )
			&& std::isfinite( value.z );
	};
	const auto& base = host ? view : pawn;
	if ( !valid( base ) || !valid( punch ) || std::abs( base.x ) > 89.0f
		|| std::abs( base.y ) > 360.0f ) return std::nullopt;
	const auto direction = base + punch;
	if ( !valid( direction ) ) return std::nullopt;
	return shot_angles{ host ? direction : base, direction };
}

inline std::uint32_t spread_seed( const foundation::vec3& angles, int tick ) noexcept
{
	struct seed_input { float pitch; float yaw; int tick; };
	const seed_input input{ quantize_angle( angles.x ), quantize_angle( angles.y ), tick };
	static_assert( sizeof( input ) == 12 );
	return foundation::sha1_first_word( std::as_bytes( std::span{ &input, 1 } ) );
}

inline foundation::vec2 spread_vector( int seed, float inaccuracy, float spread,
	float recoil_index, int item_definition, int fire_mode, int bullet_index ) noexcept
{
	foundation::source_random rng;
	rng.seed( seed );
	for ( int bullet = 0; bullet < bullet_index; ++bullet )
		for ( int draw = 0; draw < 4; ++draw ) (void)rng.uniform( );
	const auto radius = [&]( float value ) {
		if ( item_definition == 64 && fire_mode == 1 ) return 1.0f - value * value;
		if ( item_definition == 28 && recoil_index < 3.0f )
		{
			auto count = 3;
			do { --count; value *= value; } while ( static_cast<float>( count ) > recoil_index );
			return 1.0f - value;
		}
		return value;
	};
	constexpr auto two_pi = 2.0f * std::numbers::pi_v<float>;
	const auto inaccuracy_radius = radius( rng.uniform( ) ) * inaccuracy;
	const auto inaccuracy_angle = rng.uniform( 0.0f, two_pi );
	const auto spread_angle = rng.uniform( 0.0f, two_pi );
	const auto spread_radius = radius( rng.uniform( ) ) * spread;
	return {
		std::cos( spread_angle ) * spread_radius + std::cos( inaccuracy_angle ) * inaccuracy_radius,
		std::sin( spread_angle ) * spread_radius + std::sin( inaccuracy_angle ) * inaccuracy_radius };
}

}
