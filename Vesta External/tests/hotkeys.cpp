#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <core/input/hotkeys.hpp>
#include <system/storage.hpp>

namespace {
std::filesystem::path test_root;
bool check( bool condition, const char* message )
{
    if ( !condition ) std::cerr << message << '\n';
    return condition;
}
}

namespace platform::windows::runtime_storage {
std::filesystem::path area( std::string_view name ) { return test_root / name; }
}

int main( int argc, char** argv )
{
    using namespace platform::windows;
    test_root = std::filesystem::temp_directory_path( ) /
        ( "vesta-hotkeys-test-" + std::to_string( ::GetCurrentProcessId( ) ) );
    std::filesystem::create_directories( test_root );
    struct cleanup { ~cleanup( ) { std::error_code error; std::filesystem::remove_all( test_root, error ); } } guard;
    const std::string mode = argc > 1 ? argv[1] : "named";
    {
        std::ofstream seed( test_root / "hotkeys.cfg" );
        if ( mode == "numeric" ) seed << "menu = VK186\nexit = VK35\n";
        else if ( mode == "conflict" ) seed << "menu = END\nexit = END\n";
        else if ( mode == "invalid" ) seed << "menu = VK99999999999999999999999\nexit = VK27\n";
        else seed << "menu = F6\nexit = END\n";
    }
    const auto initial = lifecycle_keys( );
    const auto expected_menu = mode == "numeric" ? 186 : mode == "conflict" ? VK_END
        : mode == "invalid" ? VK_INSERT : VK_F6;
    bool ok = check( initial.menu == expected_menu, "Incorrect initial menu key" );
    ok &= check( initial.exit == ( mode == "conflict" ? VK_HOME : VK_END ), "Incorrect exit key" );
    for ( const auto invalid : { 0, VK_LBUTTON, VK_ESCAPE, int( initial.exit ), 255 } )
        ok &= check( !set_menu_key( static_cast<std::uint16_t>( invalid ) ), "Accepted invalid key" );
    ok &= check( lifecycle_keys( ).menu == initial.menu, "Rejected assignment changed binding" );
    ok &= check( set_menu_key( VK_F8 ), "Could not save binding" );
    ok &= check( lifecycle_keys( ).menu == VK_F8 && is_lifecycle_key( VK_F8 ), "Live binding not updated" );
    ok &= check( lifecycle_keys( ).exit == initial.exit, "Exit binding changed" );
    std::ifstream saved( test_root / "hotkeys.cfg" );
    const std::string contents( ( std::istreambuf_iterator<char>( saved ) ), {} );
    saved.close( );
    ok &= check( contents.find( "menu = VK119\n" ) != std::string::npos, "Binding not persisted" );
    // A failed write must keep the previously working binding.
    std::filesystem::create_directory( test_root / "hotkeys.cfg.tmp" );
    ok &= check( !set_menu_key( VK_F9 ) && lifecycle_keys( ).menu == VK_F8, "Save failure changed live binding" );
    return ok ? 0 : 1;
}
