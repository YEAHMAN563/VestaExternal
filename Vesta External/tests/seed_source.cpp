#include <simulation/seed_source.hpp>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void check( bool condition, const char* message )
{
	if ( !condition ) { std::cerr << message << '\n'; std::exit( 1 ); }
}
bool close( float a, float b ) { return std::abs( a - b ) < 1e-7f; }

// Independent Park-Miller/shuffle reference using 64-bit modulo arithmetic.
struct reference_rng
{
	std::int64_t state;
	int shuffled;
	std::array<int, 32> table{};
	explicit reference_rng( int seed )
	{
		state = std::abs( static_cast<std::int64_t>( seed ) ) % 2147483647;
		state = std::max<std::int64_t>( state, 1 );
		for ( int i = 39; i >= 0; --i )
		{
			state = state * 16807 % 2147483647;
			if ( i < 32 ) table[i] = static_cast<int>( state );
		}
		shuffled = table[0];
	}
	float draw( float max = 1.0f )
	{
		state = state * 16807 % 2147483647;
		const auto index = shuffled / 0x4000000;
		shuffled = table[index];
		table[index] = static_cast<int>( state );
		return std::min( 0.99999988f, static_cast<float>( shuffled ) * 4.6566129e-10f ) * max;
	}
};
}

int main( )
{
	using namespace simulation::seed_source;
	check( spread_seed( {0,0,0}, 1 ) == 0x3D57A2EBu, "SHA1 zero angles" );
	check( spread_seed( {-12.25f,179.75f,0}, 12345 ) == 0x4DE55EC6u, "SHA1 quantized negative pitch" );
	check( spread_seed( {10,180,0}, 99 ) == 0x03A1B36Cu, "positive 180 endpoint" );
	check( spread_seed( {10,-180,0}, 99 ) == 0x5BEAE0D3u, "negative 180 endpoint" );
	check( quantize_angle( -0.01f ) == -0.5f, "negative bucket rounds down" );
	check( quantize_angle( 360.25f ) == 0, "yaw wrap" );
	check( !bucket_is_safe( 0 ) && !bucket_is_safe( -0.5f ), "exact boundaries rejected" );
	check( !bucket_is_safe( 0.005f ) && !bucket_is_safe( 0.495f ), "near boundaries rejected" );
	check( bucket_is_safe( 0.25f ) && bucket_is_safe( -0.25f ), "bucket centers accepted" );
	check( bucket_is_safe( 0, 0 ), "guard disable" );
	check( !bucket_is_safe( std::numeric_limits<float>::infinity() ), "infinity rejected" );
	check( !bucket_is_safe( std::numeric_limits<float>::quiet_NaN(), 0 ), "NaN rejected even without guard" );
	const auto host = prepare_angles( true, {10,20,0}, {30,40,0}, {1,2,0} );
	const auto remote = prepare_angles( false, {10,20,0}, {30,40,0}, {1,2,0} );
	check( host && host->hash.x == 11 && host->hash.y == 22 && host->direction.x == 11, "host angles" );
	check( remote && remote->hash.x == 30 && remote->hash.y == 40 && remote->direction.x == 31, "remote angles" );
	check( !prepare_angles( false, {}, {90,0,0}, {} ), "invalid pawn angles fail closed" );
	check( !prepare_angles( true, {}, {}, {0,std::numeric_limits<float>::quiet_NaN(),0} ), "invalid punch" );
	for ( int seed : {0,1,-1,1234567,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()} )
	for ( int item : {7,28,64} )
	for ( int mode : {0,1} )
	for ( float recoil : {0.0f,1.5f,3.0f} )
	for ( int bullet : {0,1,7,31} )
	{
		reference_rng rng( seed );
		for ( int i = 0; i < bullet * 4; ++i ) (void)rng.draw();
		const auto transform = [&]( float r ) {
			if ( item == 64 && mode == 1 ) return 1 - r*r;
			if ( item == 28 && recoil < 3 )
			{
				for ( int count = 3; count > recoil; --count ) r *= r;
				return 1-r;
			}
			return r;
		};
		const auto ir = transform( rng.draw() ) * 0.02f;
		const auto ia = rng.draw( 2 * std::numbers::pi_v<float> );
		const auto sa = rng.draw( 2 * std::numbers::pi_v<float> );
		const auto sr = transform( rng.draw() ) * 0.03f;
		const auto actual = spread_vector( seed,0.02f,0.03f,recoil,item,mode,bullet );
		check( close( actual.x, std::cos(sa)*sr+std::cos(ia)*ir )
			&& close( actual.y, std::sin(sa)*sr+std::sin(ia)*ir ), "spread reference mismatch" );
	}
	std::cout << "Seed source: hash, guard, host/remote and 432 spread cases passed.\n";
}
