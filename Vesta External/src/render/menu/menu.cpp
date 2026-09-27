#include <stdafx.hpp>
#include <scripting/runtime.hpp>
#include <app/context.hpp>
#include <core/input/bindings.hpp>
#include <core/input/hotkeys.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/hitsound.hpp>
#include <render/chams/preview.hpp>
#include <render/chams/renderer.hpp>
#include <render/menu/localization.hpp>
#include <render/menu/menu.hpp>
#include <render/overlay/input.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <d3d11.h>
#include <chrono>

namespace
{
	// Munchi framework palette; synchronised from config::general_settings.palette.
	ImVec4 k_bg_base{ 10.0f / 255.0f, 10.0f / 255.0f, 14.0f / 255.0f, 0.95f };
	ImVec4 k_bg_panel{ 18.0f / 255.0f, 18.0f / 255.0f, 22.0f / 255.0f, 0.95f };
	ImVec4 k_bg_detail{ 12.0f / 255.0f, 12.0f / 255.0f, 16.0f / 255.0f, 0.95f };
	ImVec4 k_bg_card{ 18.0f / 255.0f, 18.0f / 255.0f, 22.0f / 255.0f, 1.0f };
	ImVec4 k_bg_popup{ 18.0f / 255.0f, 18.0f / 255.0f, 22.0f / 255.0f, 0.95f };
	ImVec4 k_bg_hover{ 1.0f, 1.0f, 1.0f, 10.0f / 255.0f };
	ImVec4 k_accent{ 179.0f / 255.0f, 143.0f / 255.0f, 228.0f / 255.0f, 1.0f };
	ImVec4 k_text_main{ 0.88f, 0.88f, 0.88f, 1.0f };
	ImVec4 k_text_muted{ 0.6f, 0.6f, 0.65f, 1.0f };
	ImVec4 k_border{ 179.0f / 255.0f, 143.0f / 255.0f, 228.0f / 255.0f, 0.15f };
	ImVec4 k_border_light{ 179.0f / 255.0f, 143.0f / 255.0f, 228.0f / 255.0f, 0.30f };
	constexpr float k_menu_width = 1000.0f;
	constexpr float k_menu_height = 700.0f;
	constexpr float k_menu_rounding = 10.0f;

	[[nodiscard]] ImVec4 menu_color( const zdraw::rgba color )
	{
		return { color.r / 255.0f, color.g / 255.0f,
			color.b / 255.0f, color.a / 255.0f };
	}

	void synchronize_menu_palette( )
	{
		const auto& palette = config::general_settings.palette;
		k_bg_base = menu_color( palette.background );
		k_bg_panel = menu_color( palette.panel );
		k_bg_card = menu_color( palette.card );
		k_bg_popup = menu_color( palette.popup );
		k_bg_hover = menu_color( palette.hover );
		k_accent = menu_color( palette.accent );
		k_text_main = menu_color( palette.text );
		k_text_muted = menu_color( palette.muted_text );
		k_border = menu_color( palette.border );
		k_border_light = k_border;
		k_border_light.w = std::min( 1.0f, k_border.w * 2.0f );
		k_bg_detail = { k_bg_panel.x * 0.667f, k_bg_panel.y * 0.667f, k_bg_panel.z * 0.73f, k_bg_panel.w };
	}

	[[nodiscard]] float current_menu_scale(
		const float display_width, const float display_height )
	{
		const auto requested = std::clamp(
			config::general_settings.menu_scale, 0.50f, 1.50f );
		const auto fit = std::min(
			display_width / k_menu_width,
			display_height / k_menu_height );
		return std::min( requested, std::max( 0.35f, fit ) );
	}

	[[nodiscard]] ImVec2 menu_transform_origin(
		const float display_width, const float display_height )
	{
		return { display_width * 0.5f, display_height * 0.5f };
	}

	[[nodiscard]] ImVec2 initial_menu_layout_position(
		const float display_width, const float display_height )
	{
		return { ( display_width - k_menu_width ) * 0.5f,
			( display_height - k_menu_height ) * 0.5f };
	}

	class menu_render_scope final
	{
	public:
		menu_render_scope( const ImVec2 display, const ImVec2 origin,
			const float scale )
			: m_viewport( ImGui::GetMainViewport( ) ),
			  m_scale( std::max( scale, 0.01f ) )
		{
			if ( !m_viewport || !GImGui )
				return;

			m_viewport_pos = m_viewport->Pos;
			m_viewport_size = m_viewport->Size;
			m_work_pos = m_viewport->WorkPos;
			m_work_size = m_viewport->WorkSize;
			m_framebuffer_scale = m_viewport->FramebufferScale;
			m_fullscreen_clip = GImGui->DrawListSharedData.ClipRectFullscreen;
			m_font_density = ImGui::GetFontRasterizerDensity( );

			const auto inverse = [ & ]( const ImVec2 point )
			{
				return ImVec2{
					origin.x + ( point.x - origin.x ) / m_scale,
					origin.y + ( point.y - origin.y ) / m_scale };
			};
			const auto virtual_min = inverse( { 0.0f, 0.0f } );
			const auto virtual_max = inverse( display );
			const auto work_min = inverse( m_work_pos );
			const auto work_max = inverse( {
				m_work_pos.x + m_work_size.x,
				m_work_pos.y + m_work_size.y } );

			m_viewport->Pos = virtual_min;
			m_viewport->Size = {
				virtual_max.x - virtual_min.x,
				virtual_max.y - virtual_min.y };
			m_viewport->WorkPos = work_min;
			m_viewport->WorkSize = {
				work_max.x - work_min.x,
				work_max.y - work_min.y };

			m_viewport->FramebufferScale = { m_scale, m_scale };
			GImGui->DrawListSharedData.ClipRectFullscreen = {
				virtual_min.x, virtual_min.y, virtual_max.x, virtual_max.y };
			ImGui::SetFontRasterizerDensity( m_scale );
			m_active = true;
		}

		~menu_render_scope( )
		{
			if ( !m_active )
				return;
			GImGui->DrawListSharedData.ClipRectFullscreen = m_fullscreen_clip;
			m_viewport->Pos = m_viewport_pos;
			m_viewport->Size = m_viewport_size;
			m_viewport->WorkPos = m_work_pos;
			m_viewport->WorkSize = m_work_size;
			m_viewport->FramebufferScale = m_framebuffer_scale;
			ImGui::SetFontRasterizerDensity( m_font_density );
		}

	private:
		ImGuiViewport* m_viewport{};
		float m_scale{ 1.0f };
		float m_font_density{ 1.0f };
		ImVec2 m_viewport_pos{};
		ImVec2 m_viewport_size{};
		ImVec2 m_work_pos{};
		ImVec2 m_work_size{};
		ImVec2 m_framebuffer_scale{};
		ImVec4 m_fullscreen_clip{};
		bool m_active{};
	};

	[[nodiscard]] bool belongs_to_menu( ImGuiWindow* window )
	{
		for ( auto* current = window; current; current = current->ParentWindow )
		{
			const auto name = std::string_view{
				current->Name ? current->Name : "" };
			if ( name == "##vesta_native_menu" || name == "##esp_visual_editor" )
				return true;
		}
		return false;
	}

	int g_foreground_vtx_start{};
	int g_foreground_cmd_start{};

	void scale_menu_draw_lists( const ImVec2 origin, const float scale )
	{
		if ( std::abs( scale - 1.0f ) < 0.0001f || !GImGui )
			return;

		for ( auto* window : GImGui->Windows )
		{
			if ( !window || window->LastFrameActive != GImGui->FrameCount
				|| !belongs_to_menu( window ) || !window->DrawList )
			{
				continue;
			}

			for ( auto& vertex : window->DrawList->VtxBuffer )
			{
				vertex.pos.x = origin.x + ( vertex.pos.x - origin.x ) * scale;
				vertex.pos.y = origin.y + ( vertex.pos.y - origin.y ) * scale;
			}
			for ( auto& command : window->DrawList->CmdBuffer )
			{
				command.ClipRect.x = origin.x + ( command.ClipRect.x - origin.x ) * scale;
				command.ClipRect.y = origin.y + ( command.ClipRect.y - origin.y ) * scale;
				command.ClipRect.z = origin.x + ( command.ClipRect.z - origin.x ) * scale;
				command.ClipRect.w = origin.y + ( command.ClipRect.w - origin.y ) * scale;
			}
		}

		// Foreground range recorded by begin_menu_foreground( ).
		auto* foreground = ImGui::GetForegroundDrawList( );
		for ( int index = g_foreground_vtx_start; index < foreground->VtxBuffer.Size; ++index )
		{
			auto& vertex = foreground->VtxBuffer[ index ];
			vertex.pos.x = origin.x + ( vertex.pos.x - origin.x ) * scale;
			vertex.pos.y = origin.y + ( vertex.pos.y - origin.y ) * scale;
		}
		for ( int index = g_foreground_cmd_start; index < foreground->CmdBuffer.Size; ++index )
		{
			auto& command = foreground->CmdBuffer[ index ];
			command.ClipRect.x = origin.x + ( command.ClipRect.x - origin.x ) * scale;
			command.ClipRect.y = origin.y + ( command.ClipRect.y - origin.y ) * scale;
			command.ClipRect.z = origin.x + ( command.ClipRect.z - origin.x ) * scale;
			command.ClipRect.w = origin.y + ( command.ClipRect.w - origin.y ) * scale;
		}
		foreground->AddCallback( ImDrawCallback_ResetRenderState, nullptr );
	}

	struct preview_hitbox_geometry
	{
		int bone{};
		ImVec2 from{};
		ImVec2 to{};
		float radius{};
	};

	constexpr std::array<int, 19> k_preview_skeleton_bones{
		1, 2, 3, 4, 6, 7, 9, 10, 11, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23 };

	std::array<ImVec2, 24> make_preview_bones( )
	{
		std::array<ImVec2, 24> result{};
		result[ 1 ] = { 0.485714f, 0.380952f };
		result[ 2 ] = { 0.489286f, 0.338095f };
		result[ 3 ] = { 0.489286f, 0.302381f };
		result[ 4 ] = { 0.489286f, 0.259524f };
		result[ 6 ] = { 0.496429f, 0.147619f };
		result[ 7 ] = { 0.482143f, 0.097619f };
		result[ 9 ] = { 0.335714f, 0.185714f };
		result[ 10 ] = { 0.282143f, 0.280952f };
		result[ 11 ] = { 0.267857f, 0.404762f };
		result[ 13 ] = { 0.650000f, 0.190476f };
		result[ 14 ] = { 0.717857f, 0.273810f };
		result[ 15 ] = { 0.739286f, 0.397619f };
		result[ 17 ] = { 0.400000f, 0.407143f };
		result[ 18 ] = { 0.389286f, 0.566667f };
		result[ 19 ] = { 0.432143f, 0.735714f };
		result[ 20 ] = { 0.582143f, 0.409524f };
		result[ 21 ] = { 0.639286f, 0.573810f };
		result[ 22 ] = { 0.689286f, 0.759524f };
		result[ 23 ] = { 0.492857f, 0.211905f };
		return result;
	}

	const auto k_preview_bones = make_preview_bones( );

	bool g_preview_orbiting{};
	const std::array<preview_hitbox_geometry, 21> k_preview_hitboxes{ {
		{ 1, { 0.414286f, 0.419048f }, { 0.575000f, 0.419048f }, 24.0f },
		{ 2, { 0.432143f, 0.340476f }, { 0.567857f, 0.342857f }, 27.0f },
		{ 3, { 0.428571f, 0.290476f }, { 0.571429f, 0.290476f }, 27.0f },
		{ 4, { 0.407143f, 0.214286f }, { 0.571429f, 0.211905f }, 25.0f },
		{ 6, { 0.457143f, 0.169048f }, { 0.528571f, 0.166667f }, 13.8f },
		{ 7, { 0.478571f, 0.083333f }, { 0.492857f, 0.121429f }, 17.5f },
		{ 8, { 0.307143f, 0.285714f }, { 0.357143f, 0.200000f }, 13.8f },
		{ 9, { 0.282143f, 0.297619f }, { 0.264286f, 0.390476f }, 9.4f },
		{ 10, { 0.267857f, 0.397619f }, { 0.257143f, 0.442857f }, 7.3f },
		{ 11, { 0.264286f, 0.428571f }, { 0.267857f, 0.445238f }, 5.0f },
		{ 12, { 0.642857f, 0.180952f }, { 0.717857f, 0.276190f }, 10.1f },
		{ 13, { 0.717857f, 0.278571f }, { 0.732143f, 0.390476f }, 9.4f },
		{ 14, { 0.735714f, 0.404762f }, { 0.750000f, 0.438095f }, 5.5f },
		{ 15, { 0.735714f, 0.421429f }, { 0.732143f, 0.435714f }, 5.0f },
		{ 17, { 0.414286f, 0.397619f }, { 0.400000f, 0.557143f }, 17.1f },
		{ 18, { 0.403571f, 0.576190f }, { 0.432143f, 0.719048f }, 15.3f },
		{ 19, { 0.453571f, 0.740476f }, { 0.385714f, 0.764286f }, 8.7f },
		{ 20, { 0.585714f, 0.426190f }, { 0.628571f, 0.566667f }, 14.2f },
		{ 21, { 0.639286f, 0.576190f }, { 0.682143f, 0.738095f }, 12.5f },
		{ 22, { 0.678571f, 0.752381f }, { 0.696429f, 0.778571f }, 10.3f },
		{ 23, { 0.490f, 0.255f }, { 0.490f, 0.195f }, 18.0f },
	} };

	ImVec2 g_menu_min{};
	ImVec2 g_menu_max{};
	ImVec2 g_settings_bounds_min{};
	ImVec2 g_settings_bounds_max{};
	bool g_settings_bounds_override{};

	[[nodiscard]] ImVec2 settings_bounds_min( )
	{
		return g_settings_bounds_override ? g_settings_bounds_min : g_menu_min;
	}

	[[nodiscard]] ImVec2 settings_bounds_max( )
	{
		return g_settings_bounds_override ? g_settings_bounds_max : g_menu_max;
	}

	class settings_bounds_scope final
	{
	public:
		settings_bounds_scope( ImVec2 min, ImVec2 max )
			: m_min( g_settings_bounds_min ), m_max( g_settings_bounds_max ),
			  m_override( g_settings_bounds_override )
		{
			g_settings_bounds_min = min;
			g_settings_bounds_max = max;
			g_settings_bounds_override = true;
		}
		~settings_bounds_scope( )
		{
			g_settings_bounds_min = m_min;
			g_settings_bounds_max = m_max;
			g_settings_bounds_override = m_override;
		}
	private:
		ImVec2 m_min{};
		ImVec2 m_max{};
		bool m_override{};
	};
	ImVec4 to_imvec( const zdraw::rgba& color )
	{
		return { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
	}

	void from_imvec( const ImVec4& value, zdraw::rgba& color )
	{
		color.r = static_cast<std::uint8_t>( std::clamp( value.x, 0.0f, 1.0f ) * 255.0f + 0.5f );
		color.g = static_cast<std::uint8_t>( std::clamp( value.y, 0.0f, 1.0f ) * 255.0f + 0.5f );
		color.b = static_cast<std::uint8_t>( std::clamp( value.z, 0.0f, 1.0f ) * 255.0f + 0.5f );
		color.a = static_cast<std::uint8_t>( std::clamp( value.w, 0.0f, 1.0f ) * 255.0f + 0.5f );
	}

	std::string key_name( int key )
	{
		switch ( key )
		{
		case 0: return render::localization::tr( "None" );
		case VK_LBUTTON: return "M1";
		case VK_RBUTTON: return "M2";
		case VK_MBUTTON: return "M3";
		case VK_XBUTTON1: return "M4";
		case VK_XBUTTON2: return "M5";
		case VK_SHIFT: case VK_LSHIFT: return "LEFT SHIFT";
		case VK_RSHIFT: return "RIGHT SHIFT";
		case VK_CONTROL: case VK_LCONTROL: return "LEFT CTRL";
		case VK_RCONTROL: return "RIGHT CTRL";
		case VK_MENU: case VK_LMENU: return "LEFT ALT";
		case VK_RMENU: return "RIGHT ALT";
		case VK_SPACE: return "SPACE";
		case VK_INSERT: return "INSERT";
		case VK_DELETE: return "DELETE";
		case VK_HOME: return "HOME";
		case VK_END: return "END";
		case VK_PRIOR: return "PAGE UP";
		case VK_NEXT: return "PAGE DOWN";
		case VK_TAB: return "TAB";
		case VK_RETURN: return "ENTER";
		case VK_BACK: return "BACKSPACE";
		case VK_CAPITAL: return "CAPS LOCK";
		case VK_NUMLOCK: return "NUM LOCK";
		case VK_SCROLL: return "SCROLL LOCK";
		case VK_PAUSE: return "PAUSE";
		case VK_UP: return "UP";
		case VK_DOWN: return "DOWN";
		case VK_LEFT: return "LEFT";
		case VK_RIGHT: return "RIGHT";
		default:
			if ( key >= '0' && key <= '9' ) return std::string( 1, static_cast<char>( key ) );
			if ( key >= 'A' && key <= 'Z' ) return std::string( 1, static_cast<char>( key ) );
			if ( key >= VK_F1 && key <= VK_F24 )
				return std::format( "F{}", key - VK_F1 + 1 );
			if ( key >= VK_NUMPAD0 && key <= VK_NUMPAD9 )
				return std::format( "NUM {}", key - VK_NUMPAD0 );
			wchar_t wide_name[64]{};
			const auto scan = ::MapVirtualKeyW( static_cast<UINT>( key ), MAPVK_VK_TO_VSC );
			if ( scan && ::GetKeyNameTextW( static_cast<LONG>( scan << 16 ),
				wide_name, static_cast<int>( std::size( wide_name ) ) ) > 0 )
			{
				char utf8[128]{};
				const auto length = ::WideCharToMultiByte( CP_UTF8, 0, wide_name, -1,
					utf8, static_cast<int>( std::size( utf8 ) ), nullptr, nullptr );
				if ( length > 1 ) return std::string( utf8, length - 1 );
			}
			return std::format( "KEY {}", key );
		}
	}

	[[nodiscard]] bool bind_input_is_down( )
	{
		for ( int button = 0; button < 5; ++button )
		{
			if ( ImGui::IsMouseDown( button ) )
				return true;
		}
		for ( int key = 1; key < 256; ++key )
		{
			const auto imgui_key = overlay_input::key_from_virtual_key( key );
			if ( imgui_key != ImGuiKey_None && ImGui::IsKeyDown( imgui_key ) )
				return true;
		}
		return false;
	}

	[[nodiscard]] int pressed_bind_key( )
	{
		constexpr int mouse_keys[]{
			VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
		for ( int button = 0; button < static_cast<int>( std::size( mouse_keys ) ); ++button )
		{
			if ( ImGui::IsMouseClicked( button, false ) )
				return mouse_keys[ button ];
		}

		for ( int key = 1; key < 256; ++key )
		{
			const auto imgui_key = overlay_input::key_from_virtual_key( key );
			if ( imgui_key != ImGuiKey_None && ImGui::IsKeyPressed( imgui_key, false ) )
				return key;
		}
		return 0;
	}

	// ------------------------------------------------------------------
	// Munchi framework (dopamina) port. Widgets, windows and cards below
	// follow framework/widgets/*.cpp and framework/gui.cpp of the supplied
	// source, adapted to this project's ImGui version and config types.
	// ------------------------------------------------------------------

	ImU32 packed( const ImVec4& color )
	{
		return ImGui::ColorConvertFloat4ToU32( color );
	}

	ImU32 clr( const ImVec4& color, float alpha = 1.0f )
	{
		auto value = color;
		value.w *= ImGui::GetStyle( ).Alpha * alpha;
		return ImGui::ColorConvertFloat4ToU32( value );
	}

	ImU32 clr_rgb( float r, float g, float b, float a = 1.0f )
	{
		return clr( ImVec4{ r / 255.0f, g / 255.0f, b / 255.0f, a } );
	}

	ImVec4 with_alpha( ImVec4 color, float alpha )
	{
		color.w = alpha;
		return color;
	}

	ImVec4 mix( const ImVec4& from, const ImVec4& to, float amount )
	{
		return {
			from.x + ( to.x - from.x ) * amount,
			from.y + ( to.y - from.y ) * amount,
			from.z + ( to.z - from.z ) * amount,
			from.w + ( to.w - from.w ) * amount };
	}

	float approach( float current, float target, float duration )
	{
		const auto step = duration > 0.0f ? ImGui::GetIO( ).DeltaTime / duration : 1.0f;
		return current + ( target - current ) * std::clamp( step, 0.0f, 1.0f );
	}

	// gui->easing( ..., dynamic_easing ): exponential approach, speed per second.
	float ease_dynamic( float& value, float target, float speed )
	{
		value = ImLerp( value, target, std::clamp( ImGui::GetIO( ).DeltaTime * speed, 0.0f, 1.0f ) );
		if ( std::abs( value - target ) < 0.0005f ) value = target;
		return value;
	}

	// gui->easing( ..., static_easing ): linear approach, speed per second.
	float ease_static( float& value, float target, float speed )
	{
		const auto step = ImGui::GetIO( ).DeltaTime * speed;
		if ( value < target ) value = std::min( value + step, target );
		else if ( value > target ) value = std::max( value - step, target );
		return value;
	}

	float quint_in_out( float t, bool rising )
	{
		if ( rising ) return t * t * t * t * t;
		const auto inv = 1.0f - t;
		return 1.0f - inv * inv * inv * inv * inv;
	}

	template<typename state_t>
	state_t& anim_state( ImGuiID id )
	{
		static std::unordered_map<ImGuiID, state_t> states{};
		return states[ id ];
	}

	void pointer_cursor_if_hovered( )
	{
		if ( ImGui::IsItemHovered( ) ) ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
	}

	ImFont* munchi_font( )
	{
		const auto* wrapper = app::context( ).overlay.fonts( ).munchi;
		return wrapper && wrapper->im_font ? wrapper->im_font : ImGui::GetFont( );
	}

	ImVec2 text_size( float size, std::string_view text )
	{
		return munchi_font( )->CalcTextSizeA( size, FLT_MAX, 0.0f, text.data( ), text.data( ) + text.size( ) );
	}

	void text( ImDrawList* draw, float size, ImVec2 position, ImU32 color, std::string_view value )
	{
		if ( value.empty( ) || ( color & IM_COL32_A_MASK ) == 0 ) return;
		draw->AddText( munchi_font( ), size, position, color, value.data( ), value.data( ) + value.size( ) );
	}

	// Framework labels are lower case ("save on exit"); keep translations,
	// lower-case ASCII, Turkish dotted I and basic Cyrillic.
	std::string ui_label( const char* label )
	{
		std::string source = render::localization::tr( label );
		std::string out{};
		out.reserve( source.size( ) );
		for ( std::size_t i = 0; i < source.size( ); ++i )
		{
			const auto c = static_cast<unsigned char>( source[ i ] );
			if ( c < 0x80 )
			{
				out.push_back( static_cast<char>( std::tolower( c ) ) );
				continue;
			}
			if ( i + 1 < source.size( ) )
			{
				const auto n = static_cast<unsigned char>( source[ i + 1 ] );
				if ( c == 0xC4 && n == 0xB0 ) { out.push_back( 'i' ); ++i; continue; }
				if ( c == 0xD0 && n >= 0x90 && n <= 0x9F ) { out.push_back( static_cast<char>( 0xD0 ) ); out.push_back( static_cast<char>( n + 0x20 ) ); ++i; continue; }
				if ( c == 0xD0 && n >= 0xA0 && n <= 0xAF ) { out.push_back( static_cast<char>( 0xD1 ) ); out.push_back( static_cast<char>( n - 0x20 ) ); ++i; continue; }
				if ( c == 0xD0 && n == 0x81 ) { out.push_back( static_cast<char>( 0xD1 ) ); out.push_back( static_cast<char>( 0x91 ) ); ++i; continue; }
				if ( c == 0xC3 && n >= 0x80 && n <= 0x9E && n != 0x97 ) { out.push_back( static_cast<char>( 0xC3 ) ); out.push_back( static_cast<char>( n + 0x20 ) ); ++i; continue; }
				if ( c == 0xC4 && ( n == 0x9E || n == 0x9A || n == 0x8C || n == 0x86 ) ) { out.push_back( static_cast<char>( 0xC4 ) ); out.push_back( static_cast<char>( n + 1 ) ); ++i; continue; }
				if ( c == 0xC5 && ( n == 0x9E || n == 0xA0 || n == 0x81 || n == 0xBD ) ) { out.push_back( static_cast<char>( 0xC5 ) ); out.push_back( static_cast<char>( n + 1 ) ); ++i; continue; }
			}
			out.push_back( source[ i ] );
		}
		return out;
	}

	// Stacked translucent rectangles: the framework's shadow technique.
	void layered_shadow( ImDrawList* draw, ImVec2 min, ImVec2 max, int layers, float blur,
		float strength, float rounding, ImVec2 offset = {}, bool grow_rounding = true,
		ImDrawFlags flags = 0 )
	{
		for ( int s = 0; s < layers; ++s )
		{
			const auto progress = static_cast<float>( s ) / layers;
			const auto alpha = ( 1.0f - progress ) * strength;
			const auto amount = blur * progress;
			draw->AddRectFilled( min - ImVec2{ amount, amount } + offset, max + ImVec2{ amount, amount } + offset,
				clr( { 0.0f, 0.0f, 0.0f, alpha } ), rounding + ( grow_rounding ? amount : 0.0f ), flags );
		}
	}

	void vertical_gradient( ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom )
	{
		draw->AddRectFilledMultiColor( min, max, top, top, bottom, bottom );
	}

	// The 16 alternating inset layers the framework uses as its "blur" texture.
	// Stacking N translucent layers is an affine blend, so each half of the
	// stack collapses into one rect with the combined alpha and colour; the
	// result matches the layered version while drawing 2 rects instead of N.
	void inset_blur( ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, int layers,
		float step, float alpha, float bright, float dark )
	{
		const auto half = std::max( 1, layers / 2 );
		for ( int first = 0; first < layers; first += half )
		{
			const auto count = std::min( half, layers - first );
			float keep = 1.0f, r = 0.0f, g = 0.0f, b = 0.0f;
			for ( int layer = first; layer < first + count; ++layer )
			{
				const auto brightness = ( layer % 2 == 0 ) ? bright : dark;
				r = r * ( 1.0f - alpha ) + brightness / 255.0f * alpha;
				g = g * ( 1.0f - alpha ) + brightness / 255.0f * alpha;
				b = b * ( 1.0f - alpha ) + ( brightness + 5.0f ) / 255.0f * alpha;
				keep *= 1.0f - alpha;
			}
			const auto combined = 1.0f - keep;
			if ( combined <= 0.0f ) continue;
			const auto inset = first * step;
			draw->AddRectFilled( min + ImVec2{ inset, inset }, max - ImVec2{ inset, inset },
				clr( { r / combined, g / combined, b / combined, combined } ),
				std::max( rounding - inset, 0.0f ) );
		}
	}

	// Framework control surface: detail layer offset by 1px under the main fill.
	void control_surface( ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding )
	{
		draw->AddRectFilled( min + ImVec2{ 1.0f, 1.0f }, max, clr( k_bg_detail ), rounding );
		draw->AddRectFilled( min, max, clr( k_bg_panel ), rounding );
	}

	// ---------------------------------------------------------------- foreground
	// Tooltips, closing dropdown lists and the ESP toggle live on the foreground
	// list like in the framework; the menu scales that range with its windows.
	ImDrawList* menu_foreground( )
	{
		return ImGui::GetForegroundDrawList( );
	}

	void begin_menu_foreground( )
	{
		auto* draw = menu_foreground( );
		draw->AddCallback( ImDrawCallback_ResetRenderState, nullptr );
		const auto& full = GImGui->DrawListSharedData.ClipRectFullscreen;
		draw->PushClipRect( { full.x, full.y }, { full.z, full.w }, false );
		g_foreground_vtx_start = draw->VtxBuffer.Size;
		g_foreground_cmd_start = std::max( 0, draw->CmdBuffer.Size - 1 );
	}

	void end_menu_foreground( )
	{
		menu_foreground( )->PopClipRect( );
	}

	// ------------------------------------------------------------------- tooltip
	void render_tooltip( std::string_view label, bool hovered )
	{
		if ( label.empty( ) ) return;
		auto& alpha = anim_state<float>( ImGui::GetID( "##munchi_tooltip" ) );
		ease_static( alpha, hovered ? 1.0f : 0.0f, 15.0f );
		if ( alpha < 0.01f ) return;

		const auto mouse = ImGui::GetMousePos( );
		const auto size = text_size( 13.0f, label );
		const auto padding = ImVec2{ 8.0f, 6.0f };
		const auto box = ImVec2{ size.x + padding.x * 2.0f, size.y + padding.y * 2.0f };
		auto position = ImVec2{ mouse.x + 10.0f, mouse.y - box.y - 10.0f };
		const auto& full = GImGui->DrawListSharedData.ClipRectFullscreen;
		if ( position.x + box.x > full.z ) position.x = full.z - box.x - 10.0f;
		if ( position.x < full.x ) position.x = full.x + 10.0f;
		if ( position.y < full.y ) position.y = mouse.y + 10.0f;

		auto* draw = menu_foreground( );
		const auto max = position + box;
		layered_shadow( draw, position, max, 6, 12.0f, 0.25f * alpha, 4.0f );
		draw->PushClipRect( position, max, true );
		vertical_gradient( draw, position, max, clr( with_alpha( k_bg_card, 0.95f * alpha ) ),
			clr( with_alpha( k_bg_base, 0.95f * alpha ) ) );
		inset_blur( draw, position, max, 4.0f, 10, 0.6f, 0.06f * alpha, 35.0f, 10.0f );
		draw->PopClipRect( );
		draw->AddRect( position, max, clr( with_alpha( k_accent, 0.15f * alpha ) ), 4.0f, 0, 1.0f );
		text( draw, 13.0f, position + padding, clr( { 1.0f, 1.0f, 1.0f, alpha } ), label );
	}

	// ------------------------------------------------------------------ checkbox
	struct checkbox_anim
	{
		float hover{};
		float active{};
		float slow_active{};
		float held{ 1.0f };
	};

	// Draws the framework checkbox at the cursor; returns the row rectangle.
	ImRect checkbox_widget( const char* label, bool& value, float reserved_right = 0.0f )
	{
		auto* window = ImGui::GetCurrentWindow( );
		const auto name = ui_label( label );
		const auto id = window->GetID( label );
		constexpr auto box = 19.0f;
		constexpr auto spacing = 10.0f;
		const auto size = text_size( 13.0f, name );
		auto pos = window->DC.CursorPos;
		pos.y += 6.0f;

		const auto available = ImGui::GetContentRegionAvail( ).x - 10.0f - reserved_right;
		const ImRect box_rect{ pos, pos + ImVec2{ box, box } };
		const ImRect full_rect{ pos, { pos.x + std::min( available, box + spacing + size.x + 10.0f ), pos.y + box } };
		ImGui::ItemSize( ImRect{ window->DC.CursorPos, full_rect.Max } );
		if ( !ImGui::ItemAdd( full_rect, id ) ) return full_rect;
		bool hovered{}, held{};
		if ( ImGui::ButtonBehavior( full_rect, id, &hovered, &held ) ) value = !value;
		pointer_cursor_if_hovered( );

		auto& anim = anim_state<checkbox_anim>( id );
		ease_static( anim.hover, ( hovered || value ) ? 1.0f : 0.0f, 20.0f );
		ease_dynamic( anim.active, value ? 1.0f : 0.0f, 20.0f );
		ease_dynamic( anim.slow_active, value ? 1.0f : 0.0f, 10.0f );
		ease_dynamic( anim.held, ( hovered && held ) ? 0.0f : 1.0f, 8.0f );

		auto* draw = window->DrawList;
		const auto shadow_strength = 0.5f + 0.2f * anim.hover;
		layered_shadow( draw, box_rect.Min, box_rect.Max, 4, 15.0f, 0.15f * shadow_strength, 4.0f,
			{ 2.5f * anim.hover, 2.5f * anim.hover }, false );
		control_surface( draw, box_rect.Min, box_rect.Max, 4.0f );
		draw->AddRect( box_rect.Min, box_rect.Max, clr( { 0.0f, 0.0f, 0.0f, 0.3f } ), 4.0f, 0, 1.0f );

		const auto center = box_rect.GetCenter( );
		if ( anim.held < 0.99f )
		{
			const auto half = box * 0.5f * anim.held;
			draw->AddRectFilled( center - ImVec2{ half, half }, center + ImVec2{ half, half },
				clr( { 10.0f / 255.0f, 10.0f / 255.0f, 14.0f / 255.0f, 0.95f }, 1.0f - anim.held ), 4.0f );
		}

		const auto quint = quint_in_out( anim.active, value );
		if ( anim.active > 0.01f )
		{
			const auto half = box * quint * 0.5f;
			draw->AddRectFilled( center - ImVec2{ half, half }, center + ImVec2{ half, half },
				clr( k_accent, quint ), 4.0f * ( 5.0f * ( 1.0f - quint ) + 1.0f ) );
		}
		if ( anim.slow_active > 0.01f )
		{
			const auto slow = quint_in_out( anim.slow_active, value );
			const auto total = 14.0f * quint;
			const auto w = total * 0.85f;
			const auto h = total * 0.70f;
			draw->PathLineTo( { std::round( center.x - w * 0.35f ), std::round( center.y - h * 0.05f ) } );
			draw->PathLineTo( { std::round( center.x - w * 0.05f ), std::round( center.y + h * 0.35f ) } );
			draw->PathLineTo( { std::round( center.x + w * 0.45f ), std::round( center.y - h * 0.50f ) } );
			draw->PathStroke( clr( { 1.0f, 1.0f, 1.0f, 1.0f }, slow ), ImDrawFlags_None, 2.5f );
		}

		const auto text_pos = ImVec2{ box_rect.Max.x + spacing + std::round( spacing * 0.5f * anim.hover ),
			box_rect.Min.y + ( box - size.y ) * 0.5f };
		draw->PushClipRect( full_rect.Min, { full_rect.Max.x, full_rect.Max.y }, true );
		text( draw, 13.0f, text_pos, clr( k_text_muted, 1.0f - quint ), name );
		text( draw, 13.0f, text_pos, clr( k_text_main, quint ), name );
		draw->PopClipRect( );
		render_tooltip( name, hovered );
		return full_rect;
	}

	void toggle_row( const char* label, bool& value )
	{
		ImGui::PushID( label );
		checkbox_widget( label, value );
		ImGui::PopID( );
	}

	// ------------------------------------------------------------- color picker
	struct color_anim
	{
		float h{ -1.0f }, s{ -1.0f }, v{ -1.0f };
		float grab[ 4 ]{};
		float box_hover{};
		float alpha{};
	};

	void color_checkerboard( ImDrawList* draw, ImVec2 min, ImVec2 max, float step, float rounding )
	{
		draw->AddRectFilled( min, max, clr_rgb( 27, 27, 32 ), rounding );
		int yi = 0;
		for ( auto y = min.y; y < max.y; y += step, ++yi )
		{
			const auto y1 = y, y2 = std::min( y + step, max.y );
			for ( auto x = min.x + ( yi & 1 ) * step; x < max.x; x += step * 2.0f )
			{
				const auto x2 = std::min( x + step, max.x );
				ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
				if ( y1 <= min.y ) { if ( x <= min.x ) corners |= ImDrawFlags_RoundCornersTopLeft; if ( x2 >= max.x ) corners |= ImDrawFlags_RoundCornersTopRight; }
				if ( y2 >= max.y ) { if ( x <= min.x ) corners |= ImDrawFlags_RoundCornersBottomLeft; if ( x2 >= max.x ) corners |= ImDrawFlags_RoundCornersBottomRight; }
				draw->AddRectFilled( { x, y1 }, { x2, y2 }, clr_rgb( 46, 46, 56 ), rounding,
					corners == ImDrawFlags_RoundCornersNone ? ImDrawFlags_RoundCornersNone : corners );
			}
		}
	}

	// Swatch with hover glow; returns true when clicked.
	bool color_swatch( ImGuiID id, ImRect rect, const zdraw::rgba& color )
	{
		auto* window = ImGui::GetCurrentWindow( );
		ImGui::ItemAdd( rect, id );
		bool hovered{}, held{};
		const auto pressed = ImGui::ButtonBehavior( rect, id, &hovered, &held );
		pointer_cursor_if_hovered( );
		auto& anim = anim_state<color_anim>( id );
		ease_static( anim.box_hover, hovered ? 1.0f : 0.0f, 15.0f );
		auto* draw = window->DrawList;
		if ( anim.box_hover > 0.01f )
		{
			for ( int i = 10; i >= 1; --i )
			{
				const auto progress = static_cast<float>( i ) / 10.0f;
				const auto radius = 8.0f * progress;
				draw->AddRectFilled( rect.Min - ImVec2{ radius, radius }, rect.Max + ImVec2{ radius, radius },
					clr( { 1.0f, 1.0f, 1.0f, ( 1.0f - progress ) * 0.08f * anim.box_hover } ), 4.0f + radius * 0.5f );
			}
		}
		color_checkerboard( draw, rect.Min, rect.Max, rect.GetWidth( ) * 0.5f, 4.0f );
		draw->AddRectFilled( rect.Min, rect.Max, clr( to_imvec( color ) ), 4.0f );
		draw->AddRectFilledMultiColor( rect.Min, rect.Max, 0, 0, clr( { 0, 0, 0, 0.3f } ), clr( { 0, 0, 0, 0.3f } ) );
		return pressed;
	}

	// Popup window header + background matching the framework's coloredit window.
	void popup_surface( std::string_view title, bool draggable )
	{
		auto* draw = ImGui::GetWindowDrawList( );
		const auto pos = ImGui::GetWindowPos( );
		const auto size = ImGui::GetWindowSize( );
		draw->PushClipRectFullScreen( );
		layered_shadow( draw, pos, pos + size, 5, 10.0f, 0.3f, 8.0f );
		draw->PopClipRect( );
		draw->AddRectFilled( pos, pos + size, clr( k_bg_popup ), 8.0f );
		draw->AddRect( pos, pos + size, clr( with_alpha( k_accent, 0.10f ) ), 8.0f, 0, 1.0f );
		draw->AddRectFilled( pos, pos + ImVec2{ size.x, 30.0f }, clr_rgb( 33, 33, 38 ), 8.0f, ImDrawFlags_RoundCornersTop );
		text( draw, 13.0f, pos + ImVec2{ 10.0f, ( 30.0f - text_size( 13.0f, title ).y ) * 0.5f },
			clr( k_text_main ), title );

		const auto cursor = ImGui::GetCursorPos( );
		ImGui::SetCursorPos( { 0.0f, 0.0f } );
		ImGui::InvisibleButton( "##drag_header", { size.x, 30.0f } );
		if ( draggable && ImGui::IsItemActive( ) && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
		{
			const auto delta = ImGui::GetIO( ).MouseDelta;
			ImGui::SetWindowPos( pos + delta );
		}
		ImGui::SetCursorPos( { cursor.x, 40.0f } );
	}

	ImVec2 clamp_popup_position( ImVec2 position, ImVec2 size )
	{
		const auto min = settings_bounds_min( );
		const auto max = settings_bounds_max( );
		position.x = std::clamp( position.x, min.x + 8.0f, std::max( min.x + 8.0f, max.x - size.x - 8.0f ) );
		position.y = std::clamp( position.y, min.y + 8.0f, std::max( min.y + 8.0f, max.y - size.y - 8.0f ) );
		return position;
	}

	void color_picker_popup( zdraw::rgba& color, ImVec2 anchor_min, ImVec2 anchor_max, ImGuiID picker_id,
		std::string_view title )
	{
		auto& anim = anim_state<color_anim>( picker_id );
		const auto open = ImGui::IsPopupOpen( "##picker" );
		ease_static( anim.alpha, open ? 1.0f : 0.0f, 8.0f );
		if ( !open ) { anim.h = anim.s = anim.v = -1.0f; return; }

		constexpr auto sv_size = 250.0f;
		constexpr auto grab = 12.0f;
		constexpr auto bar_height = 20.0f;
		constexpr auto bar_width = 3.0f;
		constexpr auto bar_padding = 2.0f;
		constexpr auto rounding = 6.0f;
		const auto popup_size = ImVec2{ 270.0f, 40.0f + sv_size + 8.0f + bar_height + 8.0f + bar_height + 10.0f };
		const auto anchor = ImVec2{ ( anchor_min.x + anchor_max.x ) * 0.5f - popup_size.x * 0.5f, anchor_max.y + 10.0f };
		ImGui::SetNextWindowPos( clamp_popup_position( anchor, popup_size ), ImGuiCond_Appearing );
		ImGui::SetNextWindowSize( popup_size, ImGuiCond_Always );
		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, ImGui::GetStyle( ).Alpha * std::max( anim.alpha, 0.01f ) );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 10.0f, 10.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 8.0f, 8.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupBorderSize, 0.0f );
		ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4{ 0, 0, 0, 0 } );
		if ( ImGui::BeginPopup( "##picker", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ) )
		{
			popup_surface( title, true );
			ImGui::SetCursorPos( { 10.0f, 40.0f } );
			auto value = to_imvec( color );
			if ( anim.h < 0.0f || anim.s < 0.0f || anim.v < 0.0f || !ImGui::IsAnyItemActive( ) )
			{
				float h{}, s{}, v{};
				ImGui::ColorConvertRGBtoHSV( value.x, value.y, value.z, h, s, v );
				if ( s > 0.001f || anim.h < 0.0f ) anim.h = h;
				anim.s = s;
				anim.v = v;
			}
			auto changed = false;
			auto* draw = ImGui::GetWindowDrawList( );

			ImGui::InvisibleButton( "sv_rect", { sv_size, sv_size } );
			auto rect = GImGui->LastItemData.Rect;
			if ( ImGui::IsItemActive( ) )
			{
				const auto mouse = ImGui::GetIO( ).MousePos;
				anim.s = ImSaturate( ( mouse.x - ( rect.Min.x + grab * 0.5f ) ) / ( rect.GetWidth( ) - grab ) );
				anim.v = 1.0f - ImSaturate( ( mouse.y - ( rect.Min.y + grab * 0.5f ) ) / ( rect.GetHeight( ) - grab ) );
				changed = true;
			}
			ease_dynamic( anim.grab[ 0 ], grab * 0.5f + anim.s * ( rect.GetWidth( ) - grab ), 20.0f );
			ease_dynamic( anim.grab[ 1 ], grab * 0.5f + ( 1.0f - anim.v ) * ( rect.GetHeight( ) - grab ), 20.0f );
			float r{}, g{}, b{};
			ImGui::ColorConvertHSVtoRGB( anim.h, 1.0f, 1.0f, r, g, b );
			draw->AddRectFilledMultiColor( rect.Min, rect.Max, clr( { 1, 1, 1, 1 } ), clr( { r, g, b, 1 } ),
				clr( { r, g, b, 1 } ), clr( { 1, 1, 1, 1 } ) );
			draw->AddRectFilledMultiColor( rect.Min, rect.Max, 0, 0, clr( { 0, 0, 0, 1 } ), clr( { 0, 0, 0, 1 } ) );
			draw->AddRect( rect.Min, rect.Max, clr( { 0, 0, 0, 0.3f } ), rounding * 0.5f );
			draw->AddCircle( rect.Min + ImVec2{ anim.grab[ 0 ], anim.grab[ 1 ] }, grab * 0.5f, clr( { 1, 1, 1, 1 } ), 30, 1.0f );

			ImGui::InvisibleButton( "hue_bar", { sv_size, bar_height } );
			rect = GImGui->LastItemData.Rect;
			if ( ImGui::IsItemActive( ) )
			{
				anim.h = ImSaturate( ( ImGui::GetIO( ).MousePos.x - ( rect.Min.x + bar_width * 0.5f ) ) / ( rect.GetWidth( ) - bar_width ) );
				changed = true;
			}
			ease_dynamic( anim.grab[ 2 ], bar_width * 0.5f + anim.h * ( rect.GetWidth( ) - bar_width ), 20.0f );
			static constexpr ImU32 hues[ 7 ]{ IM_COL32( 255, 0, 0, 255 ), IM_COL32( 255, 255, 0, 255 ), IM_COL32( 0, 255, 0, 255 ),
				IM_COL32( 0, 255, 255, 255 ), IM_COL32( 0, 0, 255, 255 ), IM_COL32( 255, 0, 255, 255 ), IM_COL32( 255, 0, 0, 255 ) };
			for ( int i = 0; i < 6; ++i )
			{
				const auto x0 = rect.Min.x + std::round( i * rect.GetWidth( ) / 6.0f );
				const auto x1 = rect.Min.x + std::round( ( i + 1 ) * rect.GetWidth( ) / 6.0f );
				const auto a = clr( ImGui::ColorConvertU32ToFloat4( hues[ i ] ) );
				const auto c = clr( ImGui::ColorConvertU32ToFloat4( hues[ i + 1 ] ) );
				draw->AddRectFilledMultiColor( { x0, rect.Min.y + bar_padding }, { x1, rect.Max.y - bar_padding }, a, c, c, a );
			}
			draw->AddRectFilled( rect.Min + ImVec2{ anim.grab[ 2 ] - bar_width * 0.5f, 0.0f },
				ImVec2{ rect.Min.x + anim.grab[ 2 ] + bar_width * 0.5f, rect.Max.y }, clr( { 1, 1, 1, 1 } ), 2.0f );

			ImGui::InvisibleButton( "alpha_bar", { sv_size, bar_height } );
			rect = GImGui->LastItemData.Rect;
			if ( ImGui::IsItemActive( ) )
			{
				value.w = ImSaturate( ( ImGui::GetIO( ).MousePos.x - ( rect.Min.x + bar_width * 0.5f ) ) / ( rect.GetWidth( ) - bar_width ) );
				from_imvec( value, color );
			}
			ease_dynamic( anim.grab[ 3 ], bar_width * 0.5f + value.w * ( rect.GetWidth( ) - bar_width ), 20.0f );
			draw->AddRectFilledMultiColor( rect.Min + ImVec2{ 0.0f, bar_padding }, rect.Max - ImVec2{ 0.0f, bar_padding },
				clr( { 1, 1, 1, 1 } ), clr( { r, g, b, 1 } ), clr( { r, g, b, 1 } ), clr( { 1, 1, 1, 1 } ) );
			draw->AddRectFilled( rect.Min + ImVec2{ anim.grab[ 3 ] - bar_width * 0.5f, 0.0f },
				ImVec2{ rect.Min.x + anim.grab[ 3 ] + bar_width * 0.5f, rect.Max.y }, clr( { 1, 1, 1, 1 } ), 2.0f );

			if ( changed )
			{
				ImGui::ColorConvertHSVtoRGB( anim.h, anim.s, anim.v, value.x, value.y, value.z );
				from_imvec( value, color );
			}
			ImGui::EndPopup( );
		}
		ImGui::PopStyleColor( );
		ImGui::PopStyleVar( 4 );
	}

	// Framework color_picker row: full width box, label left, swatch right.
	void color_row( const char* label, zdraw::rgba& color )
	{
		ImGui::PushID( label );
		auto* window = ImGui::GetCurrentWindow( );
		const auto name = ui_label( label );
		constexpr auto box_w = 24.0f, box_h = 16.0f, box_padding = 8.0f;
		auto pos = window->DC.CursorPos;
		pos.y += 6.0f;
		const auto width = ImGui::GetContentRegionAvail( ).x - 10.0f;
		const auto height = box_h + box_padding * 2.0f;
		const ImRect full{ pos, pos + ImVec2{ width, height } };
		const ImRect swatch{ { full.Max.x - box_w - box_padding, pos.y + box_padding },
			{ full.Max.x - box_padding, pos.y + box_padding + box_h } };
		ImGui::ItemSize( ImRect{ window->DC.CursorPos, full.Max } );
		const auto picker_id = window->GetID( "##picker" );
		auto* draw = window->DrawList;
		auto& anim = anim_state<color_anim>( window->GetID( "##swatch" ) );
		if ( anim.box_hover > 0.01f )
		{
			for ( int s = 0; s < 3; ++s )
			{
				const auto progress = s / 3.0f;
				const auto amount = 8.0f * progress;
				draw->AddRectFilled( full.Min - ImVec2{ amount, amount }, full.Max + ImVec2{ amount, amount },
					clr( { 0, 0, 0, ( 1.0f - progress ) * 0.08f * 0.15f * anim.box_hover } ), 6.0f );
			}
		}
		control_surface( draw, full.Min, full.Max, 6.0f );
		draw->AddRect( full.Min, full.Max, clr( { 0, 0, 0, 0.3f } ), 6.0f, 0, 1.0f );
		const auto size = text_size( 13.0f, name );
		draw->PushClipRect( full.Min, { swatch.Min.x - 6.0f, full.Max.y }, true );
		text( draw, 13.0f, { pos.x + box_padding, pos.y + ( height - size.y ) * 0.5f }, clr( k_text_main ), name );
		draw->PopClipRect( );
		if ( color_swatch( window->GetID( "##swatch" ), swatch, color ) ) ImGui::OpenPopup( "##picker" );
		render_tooltip( name, ImGui::IsItemHovered( ) );
		color_picker_popup( color, swatch.Min, swatch.Max, picker_id, name );
		ImGui::PopID( );
	}

	void toggle_color_row( const char* label, bool& value, zdraw::rgba& color )
	{
		ImGui::PushID( label );
		auto* window = ImGui::GetCurrentWindow( );
		const auto row = checkbox_widget( label, value, 34.0f );
		const auto right = window->DC.CursorPos.x + ImGui::GetContentRegionAvail( ).x - 10.0f;
		const ImRect swatch{ { right - 24.0f, row.Min.y + 1.5f }, { right, row.Min.y + 17.5f } };
		const auto picker_id = window->GetID( "##picker" );
		if ( color_swatch( window->GetID( "##swatch" ), swatch, color ) ) ImGui::OpenPopup( "##picker" );
		color_picker_popup( color, swatch.Min, swatch.Max, picker_id, ui_label( label ) );
		ImGui::PopID( );
	}

	// -------------------------------------------------------------------- slider
	struct slider_anim
	{
		float fill{ -1.0f };
		float hover{};
	};

	template<typename value_t>
	void slider_row_impl( const char* label, value_t& value, value_t minimum, value_t maximum,
		const char* suffix, value_t step )
	{
		ImGui::PushID( label );
		auto* window = ImGui::GetCurrentWindow( );
		const auto name = ui_label( label );
		const auto id = window->GetID( "##slider" );
		constexpr auto track_height = 16.0f;
		constexpr auto total_height = 34.0f;
		auto pos = window->DC.CursorPos;
		pos.y += 6.0f;
		const auto width = ImGui::GetContentRegionAvail( ).x - 10.0f;
		const ImRect full{ pos, pos + ImVec2{ width, total_height } };
		const ImRect track{ { full.Min.x, full.Min.y + 16.0f }, { full.Max.x, full.Min.y + 16.0f + track_height } };
		ImGui::ItemSize( ImRect{ window->DC.CursorPos, full.Max } );
		if ( !ImGui::ItemAdd( track, id ) ) { ImGui::PopID( ); return; }
		bool hovered{}, held{};
		ImGui::ButtonBehavior( track, id, &hovered, &held );
		pointer_cursor_if_hovered( );

		const auto range = static_cast<double>( maximum ) - static_cast<double>( minimum );
		const auto quantum = std::max( static_cast<double>( step ), std::numeric_limits<double>::epsilon( ) );
		if ( held && range > 0.0 )
		{
			const auto t = std::clamp( ( ImGui::GetIO( ).MousePos.x - track.Min.x ) / track.GetWidth( ), 0.0f, 1.0f );
			auto raw = static_cast<double>( minimum ) + t * range;
			raw = static_cast<double>( minimum ) + std::round( ( raw - static_cast<double>( minimum ) ) / quantum ) * quantum;
			value = static_cast<value_t>( std::clamp( raw, static_cast<double>( minimum ), static_cast<double>( maximum ) ) );
		}
		const auto progress = range > 0.0
			? static_cast<float>( ( static_cast<double>( value ) - static_cast<double>( minimum ) ) / range ) : 0.0f;
		const auto target_fill = track.GetWidth( ) * std::clamp( progress, 0.0f, 1.0f );
		auto& anim = anim_state<slider_anim>( id );
		if ( anim.fill < 0.0f ) anim.fill = target_fill;
		ease_dynamic( anim.fill, target_fill, 12.0f );
		ease_dynamic( anim.hover, ( hovered || held ) ? 1.0f : 0.0f, 15.0f );

		auto* draw = window->DrawList;
		const auto shadow_strength = 0.5f + 0.2f * anim.hover;
		layered_shadow( draw, track.Min, track.Max, 4, 15.0f, 0.15f * shadow_strength, 4.0f,
			{ 2.5f * anim.hover, 2.5f * anim.hover }, false );
		control_surface( draw, track.Min, track.Max, 4.0f );
		if ( anim.fill > 5.0f )
			draw->AddRectFilled( track.Min + ImVec2{ 1.0f, 1.0f }, { track.Min.x + anim.fill, track.Max.y }, clr( k_accent ), 4.0f );
		text( draw, 13.0f, full.Min, clr( k_text_main ), name );

		if ( anim.hover > 0.01f )
		{
			char buffer[ 64 ]{};
			if constexpr ( std::is_integral_v<value_t> )
				std::snprintf( buffer, sizeof( buffer ), "%d%s", static_cast<int>( value ), suffix );
			else
			{
				auto digits = 0;
				for ( auto scaled = quantum; digits < 4 && std::abs( scaled - std::round( scaled ) ) > 0.00001; ++digits ) scaled *= 10.0;
				std::snprintf( buffer, sizeof( buffer ), "%.*f%s", digits, static_cast<double>( value ), suffix );
			}
			const auto value_size = text_size( 13.0f, buffer );
			const auto padding = ImVec2{ 8.0f, 4.0f };
			const auto box = ImVec2{ value_size.x + padding.x * 2.0f, value_size.y + padding.y * 2.0f };
			const auto box_pos = ImVec2{ track.Min.x + anim.fill - box.x * 0.5f, track.Min.y - box.y - 6.0f };
			auto* top = menu_foreground( );
			layered_shadow( top, box_pos, box_pos + box, 6, 12.0f, 0.25f * anim.hover, 4.0f, {}, false );
			top->PushClipRect( box_pos, box_pos + box, true );
			vertical_gradient( top, box_pos, box_pos + box, clr( with_alpha( k_bg_card, 0.95f * anim.hover ) ),
				clr( with_alpha( k_bg_base, 0.95f * anim.hover ) ) );
			inset_blur( top, box_pos, box_pos + box, 4.0f, 8, 0.5f, 0.06f * anim.hover, 30.0f, 8.0f );
			top->PopClipRect( );
			text( top, 13.0f, box_pos + padding, clr( k_text_main, anim.hover ), buffer );
		}
		render_tooltip( name, hovered );
		ImGui::PopID( );
	}

	void slider_row( const char* label, int& value, int minimum, int maximum, const char* suffix = "" )
	{
		slider_row_impl( label, value, minimum, maximum, suffix, 1 );
	}

	void slider_row( const char* label, float& value, float minimum, float maximum, const char* suffix, float step )
	{
		slider_row_impl( label, value, minimum, maximum, suffix, step );
	}

	void slider_percent_row( const char* label, float& value )
	{
		auto percent = std::clamp( value, 0.0f, 1.0f ) * 100.0f;
		slider_row_impl( label, percent, 0.0f, 100.0f, "%", 1.0f );
		value = percent * 0.01f;
	}

	// ------------------------------------------------------------------ dropdown
	struct dropdown_anim
	{
		float hover{};
		float active{};
		ImRect list{};
		float item_height{ 28.0f };
	};

	void draw_chevron( ImDrawList* draw, ImVec2 center, float open, ImU32 color )
	{
		constexpr auto size = 8.0f;
		const auto angle = open * 3.14159265f;
		const auto rotate = [ & ]( ImVec2 p )
		{
			const auto s = std::sin( angle ), c = std::cos( angle );
			p -= center;
			return ImVec2{ p.x * c - p.y * s, p.x * s + p.y * c } + center;
		};
		const auto a = rotate( { center.x - size * 0.5f, center.y - size * 0.25f } );
		const auto b = rotate( { center.x, center.y + size * 0.25f } );
		const auto c = rotate( { center.x + size * 0.5f, center.y - size * 0.25f } );
		draw->AddLine( a, b, color, 2.0f );
		draw->AddLine( b, c, color, 2.0f );
	}

	// Box part of the framework dropdown: dim label + 28px box; returns the box rect.
	ImRect dropdown_box( ImGuiID id, std::string_view name, std::string_view shown, dropdown_anim& anim,
		bool open, bool& clicked )
	{
		auto* window = ImGui::GetCurrentWindow( );
		constexpr auto box_height = 28.0f;
		auto pos = window->DC.CursorPos;
		pos.y += 6.0f;
		const auto width = ImGui::GetContentRegionAvail( ).x - 10.0f;
		const auto label_size = text_size( 13.0f, name );
		const ImRect box{ { pos.x, pos.y + label_size.y + 2.0f }, { pos.x + width, pos.y + label_size.y + 2.0f + box_height } };
		ImGui::ItemSize( ImRect{ window->DC.CursorPos, box.Max } );
		clicked = false;
		if ( !ImGui::ItemAdd( box, id ) ) return box;
		bool hovered{}, held{};
		clicked = ImGui::ButtonBehavior( box, id, &hovered, &held );
		pointer_cursor_if_hovered( );
		ease_static( anim.hover, ( hovered || open ) ? 1.0f : 0.0f, 20.0f );
		ease_dynamic( anim.active, open ? 1.0f : 0.0f, 20.0f );

		auto* draw = window->DrawList;
		text( draw, 13.0f, pos, clr( k_text_muted ), name );
		if ( anim.hover > 0.01f )
			layered_shadow( draw, box.Min, box.Max, 4, 15.0f, 0.15f * ( 0.5f + 0.2f * anim.hover ), 4.0f, {}, false );
		control_surface( draw, box.Min, box.Max, 4.0f );
		draw->PushClipRect( box.Min, { box.Max.x - 28.0f, box.Max.y }, true );
		text( draw, 13.0f, { box.Min.x + 10.0f, box.Min.y + ( box_height - text_size( 13.0f, shown ).y ) * 0.5f },
			clr( k_text_main ), shown );
		draw->PopClipRect( );
		draw_chevron( draw, { box.Max.x - 15.0f, box.Min.y + box_height * 0.5f }, anim.active,
			clr( mix( k_text_muted, k_accent, anim.active ) ) );
		render_tooltip( name, hovered );
		return box;
	}

	// Background + highlight of the opened list, drawn scaled from its center.
	ImRect dropdown_list_rect( const ImRect& full, float alpha )
	{
		const auto center = full.GetCenter( );
		return { center + ( full.Min - center ) * alpha, center + ( full.Max - center ) * alpha };
	}

	void dropdown_list_background( ImDrawList* draw, const ImRect& list, float alpha )
	{
		draw->PushClipRect( list.Min, list.Max, true );
		layered_shadow( draw, list.Min, list.Max, 4, 15.0f, 0.15f * alpha, 4.0f, {}, false );
		draw->AddRectFilled( list.Min + ImVec2{ 1.0f, 1.0f }, list.Max, clr( k_bg_detail, alpha ), 4.0f );
		draw->AddRectFilled( list.Min, list.Max, clr( k_bg_panel, alpha ), 4.0f );
		draw->PopClipRect( );
	}

	template<typename item_t>
	void dropdown_item_text( ImDrawList* draw, const ImRect& item, float item_height, float alpha,
		bool hovered, bool selected, std::string_view label, item_t&& decorate )
	{
		const auto x = item.Min.x + 10.0f + ( hovered ? 2.5f : 0.0f ) + ( selected ? 2.5f : 0.0f );
		const auto position = ImVec2{ x + decorate( ImVec2{ x, item.Min.y }, item_height * alpha ),
			item.Min.y + ( item_height * alpha - text_size( 13.0f, label ).y ) * 0.5f };
		text( draw, 13.0f, position, clr( k_text_main, alpha * ( selected ? 0.0f : 1.0f ) ), label );
		text( draw, 13.0f, position, clr( k_accent, alpha * ( selected ? 1.0f : 0.0f ) ), label );
	}

	void select_row( const char* label, int& value, std::span<const char* const> options )
	{
		if ( options.empty( ) ) return;
		ImGui::PushID( label );
		auto* window = ImGui::GetCurrentWindow( );
		const auto name = ui_label( label );
		const auto id = window->GetID( "##dropdown" );
		auto& anim = anim_state<dropdown_anim>( id );
		const auto index = std::clamp( value, 0, static_cast<int>( options.size( ) ) - 1 );
		const auto open = ImGui::IsPopupOpen( "##options" );
		bool clicked{};
		const auto box = dropdown_box( id, name, render::localization::tr( options[ index ] ), anim, open, clicked );
		if ( clicked && !open ) ImGui::OpenPopup( "##options" );

		constexpr auto item_height = 28.0f;
		const auto list_height = item_height * options.size( );
		auto list_pos = ImVec2{ box.Min.x, box.Max.y + 4.0f };
		if ( list_pos.y + list_height > settings_bounds_max( ).y - 8.0f ) list_pos.y = box.Min.y - 4.0f - list_height;
		const ImRect full{ list_pos, list_pos + ImVec2{ box.GetWidth( ), list_height } };
		anim.list = full;

		const auto draw_items = [ & ]( ImDrawList* draw, bool interactive )
		{
			const auto alpha = std::clamp( anim.active, 0.0f, 1.0f );
			const auto list = dropdown_list_rect( full, alpha );
			dropdown_list_background( draw, list, alpha );
			draw->PushClipRect( list.Min, list.Max, true );
			for ( int i = 0; i < static_cast<int>( options.size( ) ); ++i )
			{
				const auto y = list.Min.y + item_height * alpha * i;
				const ImRect item{ { list.Min.x, y }, { list.Max.x, y + item_height * alpha } };
				auto hovered = false;
				if ( interactive && item.GetHeight( ) >= 1.0f && item.GetWidth( ) >= 1.0f )
				{
					ImGui::PushID( i );
					ImGui::SetCursorScreenPos( item.Min );
					if ( ImGui::InvisibleButton( "##option", item.GetSize( ) ) ) { value = i; ImGui::CloseCurrentPopup( ); }
					hovered = ImGui::IsItemHovered( );
					pointer_cursor_if_hovered( );
					ImGui::PopID( );
				}
				const auto selected = i == value;
				if ( hovered || selected )
				{
					const auto corners = i == 0 ? ImDrawFlags_RoundCornersTop
						: i == static_cast<int>( options.size( ) ) - 1 ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersNone;
					draw->AddRectFilled( item.Min, item.Max, clr( k_accent, ( selected ? 0.25f : 0.12f ) * alpha ),
						corners == ImDrawFlags_RoundCornersNone ? 0.0f : 4.0f, corners );
				}
				dropdown_item_text( draw, item, item_height, alpha, hovered, selected,
					render::localization::tr( options[ i ] ), [ ]( ImVec2, float ) { return 0.0f; } );
			}
			draw->PopClipRect( );
		};

		ImGui::SetNextWindowPos( full.Min, ImGuiCond_Always );
		ImGui::SetNextWindowSize( full.GetSize( ), ImGuiCond_Always );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupBorderSize, 0.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f } );
		if ( ImGui::BeginPopup( "##options", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ) )
		{
			draw_items( ImGui::GetWindowDrawList( ), true );
			ImGui::EndPopup( );
		}
		else if ( anim.active > 0.01f )
		{
			draw_items( menu_foreground( ), false );
		}
		ImGui::PopStyleVar( 3 );
		ImGui::PopID( );
	}

	void multiselect_row( const char* label, int& mask, std::span<const std::pair<const char*, int>> options, int all_mask )
	{
		if ( options.empty( ) ) return;
		ImGui::PushID( label );
		auto* window = ImGui::GetCurrentWindow( );
		const auto name = ui_label( label );
		const auto id = window->GetID( "##multiselect" );
		auto& anim = anim_state<dropdown_anim>( id );

		std::string summary{};
		if ( ( mask & all_mask ) == all_mask ) summary = render::localization::tr( "All" );
		else
		{
			for ( const auto& [ option, bit ] : options )
				if ( mask & bit ) summary += ( summary.empty( ) ? "" : ", " ) + std::string( render::localization::tr( option ) );
			if ( summary.empty( ) ) summary = render::localization::tr( "None" );
		}

		const auto open = ImGui::IsPopupOpen( "##multi_options" );
		bool clicked{};
		const auto box = dropdown_box( id, name, summary, anim, open, clicked );
		if ( clicked && !open ) ImGui::OpenPopup( "##multi_options" );

		constexpr auto item_height = 28.0f;
		const auto list_height = item_height * options.size( );
		auto list_pos = ImVec2{ box.Min.x, box.Max.y + 4.0f };
		if ( list_pos.y + list_height > settings_bounds_max( ).y - 8.0f ) list_pos.y = box.Min.y - 4.0f - list_height;
		const ImRect full{ list_pos, list_pos + ImVec2{ box.GetWidth( ), list_height } };

		const auto draw_items = [ & ]( ImDrawList* draw, bool interactive )
		{
			const auto alpha = std::clamp( anim.active, 0.0f, 1.0f );
			const auto list = dropdown_list_rect( full, alpha );
			dropdown_list_background( draw, list, alpha );
			draw->PushClipRect( list.Min, list.Max, true );
			for ( int i = 0; i < static_cast<int>( options.size( ) ); ++i )
			{
				const auto y = list.Min.y + item_height * alpha * i;
				const ImRect item{ { list.Min.x, y }, { list.Max.x, y + item_height * alpha } };
				const auto bit = options[ i ].second;
				auto hovered = false;
				if ( interactive && item.GetHeight( ) >= 1.0f && item.GetWidth( ) >= 1.0f )
				{
					ImGui::PushID( i );
					ImGui::SetCursorScreenPos( item.Min );
					if ( ImGui::InvisibleButton( "##multi_option", item.GetSize( ) ) ) mask ^= bit;
					hovered = ImGui::IsItemHovered( );
					pointer_cursor_if_hovered( );
					ImGui::PopID( );
				}
				const auto selected = ( mask & bit ) != 0;
				if ( hovered )
					draw->AddRectFilled( item.Min, item.Max, clr( k_accent, 0.12f * alpha ), 0.0f );
				dropdown_item_text( draw, item, item_height, alpha, hovered, selected,
					render::localization::tr( options[ i ].first ), [ & ]( ImVec2 at, float height )
					{
						const auto check_min = ImVec2{ at.x, at.y + ( height - 14.0f * alpha ) * 0.5f };
						const auto check_max = check_min + ImVec2{ 14.0f, 14.0f } * alpha;
						draw->AddRectFilled( check_min, check_max, clr( selected ? k_accent : k_bg_detail, alpha ), 3.0f );
						draw->AddRect( check_min, check_max, clr( { 0, 0, 0, 0.3f * alpha } ), 3.0f );
						if ( selected )
						{
							const auto center = ( check_min + check_max ) * 0.5f;
							const auto size = 10.0f * alpha;
							draw->PathLineTo( { center.x - size * 0.30f, center.y - size * 0.05f } );
							draw->PathLineTo( { center.x - size * 0.05f, center.y + size * 0.30f } );
							draw->PathLineTo( { center.x + size * 0.40f, center.y - size * 0.40f } );
							draw->PathStroke( clr( { 1, 1, 1, alpha } ), ImDrawFlags_None, 2.0f );
						}
						return 22.0f;
					} );
			}
			draw->PopClipRect( );
		};

		ImGui::SetNextWindowPos( full.Min, ImGuiCond_Always );
		ImGui::SetNextWindowSize( full.GetSize( ), ImGuiCond_Always );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupBorderSize, 0.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f } );
		if ( ImGui::BeginPopup( "##multi_options", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ) )
		{
			draw_items( ImGui::GetWindowDrawList( ), true );
			ImGui::EndPopup( );
		}
		else if ( anim.active > 0.01f )
		{
			draw_items( menu_foreground( ), false );
		}
		ImGui::PopStyleVar( 3 );
		ImGui::PopID( );
	}

	// -------------------------------------------------------------------- button
	struct button_anim
	{
		float hover{};
		float active{};
	};

	enum class row_action_icon
	{
		none,
		copy,
		check,
		save,
		folder
	};

	// Framework button: full width, centered 16px label; the row label is its text.
	bool button_row( const char* label, const char* text_value, row_action_icon = row_action_icon::none )
	{
		( void )text_value;
		ImGui::PushID( label );
		auto* window = ImGui::GetCurrentWindow( );
		const auto name = ui_label( label );
		const auto id = window->GetID( "##button" );
		constexpr auto height = 22.0f;
		auto pos = window->DC.CursorPos;
		pos.y += 6.0f;
		const auto width = ImGui::GetContentRegionAvail( ).x - 10.0f;
		auto& anim = anim_state<button_anim>( id );
		const auto shrink = width * 0.1f * anim.active;
		const ImRect full{ pos, pos + ImVec2{ width, height } };
		const ImRect rect{ { pos.x + shrink * 0.5f, pos.y }, { pos.x + width - shrink * 0.5f, pos.y + height } };
		ImGui::ItemSize( ImRect{ window->DC.CursorPos, full.Max } );
		auto pressed = false;
		if ( ImGui::ItemAdd( full, id ) )
		{
			bool hovered{}, held{};
			pressed = ImGui::ButtonBehavior( full, id, &hovered, &held );
			pointer_cursor_if_hovered( );
			ease_static( anim.hover, hovered ? 1.0f : 0.0f, 20.0f );
			ease_dynamic( anim.active, held ? 1.0f : 0.0f, 20.0f );

			auto* draw = window->DrawList;
			const auto shadow_strength = 0.5f + 0.2f * anim.hover;
			layered_shadow( draw, rect.Min, rect.Max, 4, 15.0f, 0.15f * shadow_strength, 4.0f,
				{ 2.5f * anim.hover, 2.5f * anim.hover }, false );
			control_surface( draw, rect.Min, rect.Max, 4.0f );
			const auto size = text_size( 16.0f, name );
			const auto position = ImVec2{ rect.Min.x + ( rect.GetWidth( ) - size.x ) * 0.5f, rect.Min.y + ( height - size.y ) * 0.5f };
			draw->PushClipRect( rect.Min, rect.Max, true );
			text( draw, 16.0f, position, clr( k_text_main ), name );
			text( draw, 16.0f, position, clr( k_accent, anim.active ), name );
			draw->PopClipRect( );
			render_tooltip( name, hovered );
		}
		ImGui::PopID( );
		return pressed;
	}

	// ---------------------------------------------------------------- text input
	int filter_filename_char( ImGuiInputTextCallbackData* data )
	{
		const auto c = data->EventChar;
		if ( c < 32 || c == '\\' || c == '/' || c == ':' || c == '*' ||
			c == '?' || c == '"' || c == '<' || c == '>' || c == '|' )
			return 1;
		return 0;
	}

	void text_input_row( const char* label, char* buffer, std::size_t size )
	{
		ImGui::PushID( label );
		auto* window = ImGui::GetCurrentWindow( );
		const auto name = ui_label( label );
		constexpr auto box_height = 28.0f;
		auto pos = window->DC.CursorPos;
		pos.y += 6.0f;
		const auto width = ImGui::GetContentRegionAvail( ).x - 10.0f;
		const auto label_size = text_size( 13.0f, name );
		const ImRect box{ { pos.x, pos.y + label_size.y + 2.0f }, { pos.x + width, pos.y + label_size.y + 2.0f + box_height } };
		auto* draw = window->DrawList;
		text( draw, 13.0f, pos, clr( k_text_muted ), name );
		control_surface( draw, box.Min, box.Max, 4.0f );
		ImGui::SetCursorScreenPos( { box.Min.x + 10.0f, box.Min.y + ( box_height - ImGui::GetTextLineHeight( ) ) * 0.5f } );
		ImGui::SetNextItemWidth( width - 20.0f );
		ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4{ 0, 0, 0, 0 } );
		ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4{ 0, 0, 0, 0 } );
		ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4{ 0, 0, 0, 0 } );
		ImGui::PushStyleColor( ImGuiCol_Text, k_text_main );
		ImGui::PushStyleColor( ImGuiCol_TextSelectedBg, with_alpha( k_accent, 0.35f ) );
		ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, { 0.0f, 0.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
		ImGui::InputText( "##text_input", buffer, size, ImGuiInputTextFlags_CallbackCharFilter, filter_filename_char );
		const auto active = ImGui::IsItemActive( );
		ImGui::PopStyleVar( 2 );
		ImGui::PopStyleColor( 5 );
		draw->AddRect( box.Min, box.Max, clr( k_accent, active ? 0.35f : 0.0f ), 4.0f );
		ImGui::SetCursorScreenPos( { pos.x, pos.y - 6.0f } );
		ImGui::ItemSize( ImRect{ { pos.x, pos.y - 6.0f }, box.Max } );
		ImGui::PopID( );
	}

	// ------------------------------------------------------------------- keybind
	int* g_listening_key{};
	int g_listening_seen_frame{ -1 };
	int g_listening_start_frame{};
	bool g_listening_armed{};

	struct keybind_anim
	{
		float hover{};
		float listening{};
	};

	// Framework keybind box (right aligned, accent border, pulsing dots while listening).
	void keybind_row( const char* label, int& value )
	{
		ImGui::PushID( label );
		auto* window = ImGui::GetCurrentWindow( );
		const auto name = ui_label( label );
		const auto id = window->GetID( "##keybind" );
		auto& anim = anim_state<keybind_anim>( id );
		const auto listening = g_listening_key == &value;
		const auto key_text = key_name( value );
		const auto key_size = text_size( 12.0f, key_text );
		constexpr auto row_height = 19.0f;
		const auto box_w = std::max( 38.0f, key_size.x + 16.0f );
		constexpr auto box_h = 18.0f;
		auto pos = window->DC.CursorPos;
		pos.y += 6.0f;
		const auto width = ImGui::GetContentRegionAvail( ).x - 10.0f;
		const ImRect row{ pos, pos + ImVec2{ width, row_height } };
		const ImRect box{ { row.Max.x - box_w, pos.y + ( row_height - box_h ) * 0.5f },
			{ row.Max.x, pos.y + ( row_height + box_h ) * 0.5f } };
		ImGui::ItemSize( ImRect{ window->DC.CursorPos, row.Max } );
		if ( !ImGui::ItemAdd( box, id ) ) { ImGui::PopID( ); return; }
		bool hovered{}, held{};
		if ( ImGui::ButtonBehavior( box, id, &hovered, &held ) && !listening )
		{
			g_listening_key = &value;
			g_listening_start_frame = ImGui::GetFrameCount( );
			g_listening_seen_frame = ImGui::GetFrameCount( );
			g_listening_armed = false;
		}
		pointer_cursor_if_hovered( );
		anim.hover += ( ( hovered ? 1.0f : 0.0f ) - anim.hover ) * std::min( ImGui::GetIO( ).DeltaTime * 15.0f, 1.0f );
		if ( listening ) anim.listening += ImGui::GetIO( ).DeltaTime * 3.0f;
		else anim.listening = 0.0f;

		auto* draw = window->DrawList;
		text( draw, 13.0f, { row.Min.x, row.Min.y + ( row_height - text_size( 13.0f, name ).y ) * 0.5f }, clr( k_text_main ), name );
		for ( int s = 0; s < 4; ++s )
		{
			const auto p = s / 4.0f;
			const auto blur = 8.0f * p;
			draw->AddRectFilled( box.Min - ImVec2{ blur, blur }, box.Max + ImVec2{ blur, blur },
				clr( { 0, 0, 0, ( 1.0f - p ) * 45.0f / 255.0f * ( 1.0f + anim.hover * 0.6f ) } ), 4.0f );
		}
		draw->AddRectFilled( box.Min + ImVec2{ 1.0f, 1.0f }, box.Max, clr_rgb( 10, 10, 14, 240.0f / 255.0f ), 4.0f );
		draw->AddRectFilled( box.Min, box.Max, clr_rgb( 18 + anim.hover * 8, 18 + anim.hover * 6, 22 + anim.hover * 10, 240.0f / 255.0f ), 4.0f );
		draw->AddRect( box.Min, box.Max, clr( k_accent, listening ? 0.25f : 0.12f + anim.hover * 0.28f ), 4.0f, 0, 1.0f );
		if ( listening )
		{
			const auto center = box.GetCenter( );
			for ( int d = 0; d < 3; ++d )
			{
				const auto phase = anim.listening * 0.6f + d * 0.7f;
				const auto alpha = 0.3f + 0.7f * ( 0.5f + 0.5f * std::sin( phase * 0.8f ) );
				draw->AddCircleFilled( { center.x + ( d - 1 ) * 7.0f, center.y }, 2.0f, clr( k_accent, alpha ) );
			}
		}
		else
		{
			const auto text_pos = ImVec2{ box.Min.x + ( box_w - key_size.x ) * 0.5f, box.Min.y + ( box_h - key_size.y ) * 0.5f };
			text( draw, 12.0f, text_pos, clr_rgb( 130, 130, 138 ), key_text );
			if ( anim.hover > 0.01f ) text( draw, 12.0f, text_pos, clr_rgb( 210, 200, 235, anim.hover * 200.0f / 255.0f ), key_text );
		}
		render_tooltip( name, hovered );

		if ( g_listening_key == &value && ImGui::GetFrameCount( ) > g_listening_start_frame )
		{
			g_listening_seen_frame = ImGui::GetFrameCount( );
			if ( !g_listening_armed )
			{
				if ( !bind_input_is_down( ) ) g_listening_armed = true;
			}
			else if ( const int key = pressed_bind_key( ) )
			{
				value = key == VK_ESCAPE ? 0 : key;
				g_listening_key = nullptr;
				g_listening_armed = false;
			}
		}
		ImGui::PopID( );
	}

	// ------------------------------------------------------ label + value rows
	std::vector<ImRect> g_row_stack{};

	// Label on the left; the cursor is moved to the right-hand control area.
	void begin_row( const char* label, float control_width )
	{
		ImGui::PushID( label );
		auto* window = ImGui::GetCurrentWindow( );
		const auto name = ui_label( label );
		auto pos = window->DC.CursorPos;
		pos.y += 6.0f;
		const auto width = ImGui::GetContentRegionAvail( ).x - 10.0f;
		const ImRect row{ pos, pos + ImVec2{ width, 19.0f } };
		g_row_stack.push_back( ImRect{ window->DC.CursorPos, row.Max } );
		auto* draw = window->DrawList;
		const auto label_max = row.Max.x - control_width - 8.0f;
		draw->PushClipRect( row.Min, { std::max( row.Min.x, label_max ), row.Max.y }, true );
		text( draw, 13.0f, { row.Min.x, row.Min.y + ( 19.0f - text_size( 13.0f, name ).y ) * 0.5f }, clr( k_text_main ), name );
		draw->PopClipRect( );
		ImGui::SetCursorScreenPos( { row.Max.x - control_width, row.Min.y + ( 19.0f - ImGui::GetTextLineHeight( ) ) * 0.5f } );
	}

	void clipped_row_text( const std::string_view value, const ImVec4 color = k_text_muted )
	{
		auto* window = ImGui::GetCurrentWindow( );
		const auto minimum = window->DC.CursorPos;
		const auto maximum_x = g_row_stack.empty( ) ? minimum.x + ImGui::GetContentRegionAvail( ).x : g_row_stack.back( ).Max.x;
		const auto available = std::max( 1.0f, maximum_x - minimum.x );
		ImGui::PushStyleColor( ImGuiCol_Text, color );
		ImGui::RenderTextEllipsis( window->DrawList, minimum, { maximum_x, minimum.y + ImGui::GetTextLineHeight( ) },
			maximum_x, value.data( ), value.data( ) + value.size( ), nullptr );
		ImGui::PopStyleColor( );
		ImGui::InvisibleButton( "##clipped_row_text", { available, ImGui::GetTextLineHeight( ) } );
		if ( ImGui::IsItemHovered( ) && ImGui::CalcTextSize( value.data( ), value.data( ) + value.size( ) ).x > available )
			ImGui::SetTooltip( "%.*s", static_cast<int>( value.size( ) ), value.data( ) );
	}

	void end_row( )
	{
		if ( g_row_stack.empty( ) ) return;
		const auto row = g_row_stack.back( );
		g_row_stack.pop_back( );
		ImGui::SetCursorScreenPos( row.Min );
		ImGui::ItemSize( row );
		ImGui::PopID( );
	}

	// ------------------------------------------------------------ settings popups
	std::unordered_map<ImGuiID, float> g_popup_heights{};

	// Opens a framework-styled window next to the anchor holding more rows.
	template<typename callback_t>
	void settings_popup( ImVec2 anchor_min, ImVec2 anchor_max, std::string_view title, int rows, callback_t&& callback )
	{
		auto* parent = ImGui::GetCurrentWindow( );
		const auto popup_id = ImGui::GetID( "##settings" );
		auto& alpha = anim_state<float>( popup_id );
		const auto open = ImGui::IsPopupOpen( "##settings" );
		ease_static( alpha, open ? 1.0f : 0.0f, 8.0f );
		if ( !open ) return;

		constexpr auto popup_width = 300.0f;
		const auto known = g_popup_heights.find( popup_id );
		const auto height = known != g_popup_heights.end( ) ? known->second : 50.0f + rows * 44.0f;
		const auto size = ImVec2{ popup_width, height };
		const auto bounds_min = settings_bounds_min( );
		const auto bounds_max = settings_bounds_max( );
		const auto nested = parent && ( parent->Flags & ImGuiWindowFlags_Popup ) != 0;
		ImVec2 position{};
		if ( nested )
		{
			const auto right = parent->Pos.x + parent->Size.x + 8.0f;
			const auto left = parent->Pos.x - size.x - 8.0f;
			position = { right + size.x <= bounds_max.x - 8.0f ? right : left, anchor_min.y - 12.0f };
		}
		else
		{
			position = { anchor_max.x - size.x, anchor_max.y + 10.0f };
			if ( position.y + size.y > bounds_max.y - 8.0f ) position.y = anchor_min.y - size.y - 10.0f;
		}
		position.x = std::clamp( position.x, bounds_min.x + 8.0f, std::max( bounds_min.x + 8.0f, bounds_max.x - size.x - 8.0f ) );
		position.y = std::clamp( position.y, bounds_min.y + 8.0f, std::max( bounds_min.y + 8.0f, bounds_max.y - size.y - 8.0f ) );
		ImGui::SetNextWindowPos( position, ImGuiCond_Always );
		ImGui::SetNextWindowSize( { popup_width, 0.0f }, ImGuiCond_Always );
		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, ImGui::GetStyle( ).Alpha * std::max( alpha, 0.01f ) );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 10.0f, 10.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 8.0f, 4.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupBorderSize, 0.0f );
		ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4{ 0, 0, 0, 0 } );
		if ( ImGui::BeginPopup( "##settings", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
			ImGuiWindowFlags_NoBackground ) )
		{
			popup_surface( title, false );
			ImGui::SetCursorPosY( 36.0f );
			callback( );
			ImGui::Dummy( { popup_width - 20.0f, 6.0f } );
			g_popup_heights[ popup_id ] = ImGui::GetWindowSize( ).y;
			ImGui::EndPopup( );
		}
		ImGui::PopStyleColor( );
		ImGui::PopStyleVar( 4 );
	}

	// Gear button (IconsV2 U+F051) opening the row's settings popup.
	bool settings_icon( ImGuiID id, ImRect rect )
	{
		ImGui::ItemAdd( rect, id );
		bool hovered{}, held{};
		const auto pressed = ImGui::ButtonBehavior( rect, id, &hovered, &held );
		pointer_cursor_if_hovered( );
		auto& anim = anim_state<float>( id );
		ease_dynamic( anim, ( hovered || ImGui::IsPopupOpen( "##settings" ) ) ? 1.0f : 0.0f, 15.0f );
		constexpr std::string_view gear{ "\xEF\x81\x91" };
		const auto size = text_size( 15.0f, gear );
		text( ImGui::GetWindowDrawList( ), 15.0f, rect.GetCenter( ) - size * 0.5f,
			clr( mix( ImVec4{ 120.0f / 255.0f, 120.0f / 255.0f, 125.0f / 255.0f, 1.0f }, k_accent, anim ) ), gear );
		return pressed;
	}

	template<typename callback_t>
	void settings_popup_row( const char* label, int rows, callback_t&& callback )
	{
		ImGui::PushID( label );
		auto* window = ImGui::GetCurrentWindow( );
		const auto name = ui_label( label );
		auto pos = window->DC.CursorPos;
		pos.y += 6.0f;
		const auto width = ImGui::GetContentRegionAvail( ).x - 10.0f;
		const ImRect row{ pos, pos + ImVec2{ width, 19.0f } };
		ImGui::ItemSize( ImRect{ window->DC.CursorPos, row.Max } );
		const ImRect icon{ { row.Max.x - 20.0f, row.Min.y }, row.Max };
		const auto row_id = window->GetID( "##settings_row" );
		ImGui::ItemAdd( row, row_id );
		bool hovered{}, held{};
		const auto row_pressed = ImGui::ButtonBehavior( { row.Min, { icon.Min.x, row.Max.y } }, row_id, &hovered, &held );
		pointer_cursor_if_hovered( );
		auto& hover = anim_state<float>( row_id );
		ease_static( hover, hovered ? 1.0f : 0.0f, 20.0f );
		auto* draw = window->DrawList;
		text( draw, 13.0f, { row.Min.x + std::round( 5.0f * hover ), row.Min.y + ( 19.0f - text_size( 13.0f, name ).y ) * 0.5f },
			clr( mix( k_text_muted, k_text_main, 0.6f + 0.4f * hover ) ), name );
		if ( settings_icon( window->GetID( "##settings_icon" ), icon ) || row_pressed ) ImGui::OpenPopup( "##settings" );
		render_tooltip( name, hovered );
		settings_popup( row.Min, row.Max, name, rows, std::forward<callback_t>( callback ) );
		ImGui::PopID( );
	}

	template<typename callback_t>
	void toggle_popup_row( const char* label, bool& value, int rows, callback_t&& callback )
	{
		ImGui::PushID( label );
		auto* window = ImGui::GetCurrentWindow( );
		const auto row = checkbox_widget( label, value, 30.0f );
		const auto right = window->DC.CursorPos.x + ImGui::GetContentRegionAvail( ).x - 10.0f;
		const ImRect icon{ { right - 20.0f, row.Min.y }, { right, row.Max.y } };
		if ( settings_icon( window->GetID( "##settings_icon" ), icon ) ) ImGui::OpenPopup( "##settings" );
		settings_popup( icon.Min, icon.Max, ui_label( label ), rows, std::forward<callback_t>( callback ) );
		ImGui::PopID( );
	}

	void aim_parts_row( int& mask )
	{
		static constexpr std::pair<const char*, int> opts[]{
			{ "Head", config::combat_profile::aim_part::head },
			{ "Body", config::combat_profile::aim_part::body },
			{ "Arms", config::combat_profile::aim_part::arms },
			{ "Legs", config::combat_profile::aim_part::legs },
		};
		multiselect_row( "Hitboxes", mask, opts, config::combat_profile::aim_part::all );
	}

	void humanizer_preview( int amount, int smoothing,
		const config::combat_profile::humanizer_settings& settings )
	{
		const auto origin = ImGui::GetCursorScreenPos( );
		const auto width = ImGui::GetContentRegionAvail( ).x;
		constexpr auto height = 76.0f;
		ImGui::InvisibleButton( "##humanizer_preview", { width, height } );
		auto* draw = ImGui::GetWindowDrawList( );
		const auto left = origin + ImVec2{ 5.0f, height * 0.68f };
		const auto right = origin + ImVec2{ width - 8.0f, height * 0.38f };
		const auto h = std::clamp( amount / 100.0f, 0.0f, 1.0f );
		const auto step_speed = std::clamp( settings.max_step / 15.0f, 0.25f, 4.0f );
		const auto duration = std::clamp(
			( 0.34f + smoothing * 0.032f ) / std::sqrt( step_speed ), 0.28f, 2.5f );
		const auto reaction = ( settings.reaction_min_ms + settings.reaction_max_ms ) * 0.0005f;
		const auto cycle = reaction + duration + 0.45f;
		const auto elapsed = static_cast<float>( ImGui::GetTime( ) );
		const auto local = std::fmod( elapsed, cycle );
		const auto cycle_index = static_cast<int>( elapsed / cycle );
		const auto progress = local <= reaction ? 0.0f
			: std::clamp( ( local - reaction ) / duration, 0.0f, 1.0f );
		const auto gravity = std::clamp( settings.gravity / 9.0f, 0.15f, 2.25f );
		const auto eased = 1.0f - std::pow( 1.0f - progress, gravity );
		const auto bend = ( settings.curve - 0.5f ) * 38.0f * h;
		const auto damping = std::clamp( settings.damping, 0.0f, 1.0f );
		const auto wind = settings.wind * 0.85f * h;
		const auto overshoot_cycle = ( cycle_index * 37 ) % 100
			< static_cast<int>( settings.overshoot_chance );
		const auto overshoot = overshoot_cycle
			? settings.overshoot_amount * 34.0f * h : 0.0f;
		const auto path_point = [ & ]( float t )
		{
			const auto settle = std::sin( t * 3.14159265f );
			const auto overshoot_shape = t < 0.78f
				? std::sin( t / 0.78f * 1.5707963f )
				: 1.0f - ( t - 0.78f ) / 0.22f;
			const auto x = std::lerp( left.x, right.x, t ) + overshoot * overshoot_shape;
			const auto arc = settle * bend;
			const auto oscillation = std::sin( t * 18.0f + 0.7f ) * wind
				* std::pow( 1.0f - t, 0.35f + damping * 2.5f );
			const auto micro = std::sin( t * 71.0f + 1.9f )
				* settings.jitter * 1.4f * h * settle;
			return ImVec2{ x, std::lerp( left.y, right.y, t ) - arc + oscillation + micro };
		};
		ImVec2 previous = path_point( 0.0f );
		for ( int segment = 1; segment <= 48; ++segment )
		{
			const auto t = segment / 48.0f;
			const auto next = path_point( t );
			draw->AddLine( previous, next, IM_COL32( 255, 255, 255, 31 ), 1.0f );
			previous = next;
		}
		const auto deadzone = 2.5f + settings.deadzone * 3.0f;
		draw->AddCircle( right, deadzone, IM_COL32( 255, 255, 255, 48 ), 20, 1.0f );
		draw->AddCircleFilled( right, 1.7f, IM_COL32( 232, 235, 244, 180 ), 16 );
		for ( int tail = 8; tail >= 0; --tail )
		{
			const auto t = std::max( 0.0f, eased - tail * ( 0.012f + damping * 0.01f ) );
			const auto alpha = static_cast<int>( 28 + ( 8 - tail ) * 18 );
			draw->AddCircleFilled( path_point( t ), tail == 0 ? 2.8f : 1.4f,
				IM_COL32( 214, 226, 255, alpha ), 16 );
		}
	}

	constexpr int k_bar_settings_rows = 7;
	constexpr int k_weapon_settings_rows = 4;
	constexpr int k_info_flag_settings_rows = 4;

	void dock_player_bar( config::visual_profile::player::layout_element& layout, int dock )
	{
		const auto scale = layout.scale;
		switch ( dock )
		{
		case 0: layout = { -0.07f, 0.50f, scale }; break;
		case 1: layout = { 0.50f, -0.04f, scale }; break;
		case 2: layout = { 0.50f, 1.012f, scale }; break;
		case 3: layout = { 1.04f, 0.50f, scale }; break;
		default: break;
		}
	}

	template<typename bar_t>
	void player_bar_settings_rows( bar_t& bar,
		config::visual_profile::player::layout_element& layout )
	{
		int position = static_cast<int>( bar.position );
		const auto previous_position = position;
		static constexpr const char* positions[]{ "Left", "Top", "Bottom", "Right" };
		select_row( "Position", position, positions );
		bar.position = static_cast<typename bar_t::position_type>( position );
		if ( position != previous_position ) dock_player_bar( layout, position );
		slider_row( "Thickness", bar.thickness, 1.0f, 12.0f, " px", 0.5f );
		toggle_popup_row( "Outline", bar.outline, 2, [ & ]
			{
				slider_row( "Thickness", bar.outline_thickness, 0.5f, 4.0f, " px", 0.5f );
				color_row( "Color", bar.outline_color );
			} );
		toggle_popup_row( "Gradient", bar.gradient, 2, [ & ]
			{
				color_row( "Full Color", bar.full_color );
				color_row( "Low Color", bar.low_color );
			} );
		bool segmented = bar.segments > 1;
		toggle_popup_row( "Segments", segmented, 2, [ & ]
			{
				slider_row( "Count", bar.segments, 2, 10 );
				slider_row( "Gap", bar.segment_gap, 0.0f, 4.0f, " px", 0.5f );
			} );
		if ( segmented && bar.segments < 2 ) bar.segments = 4;
		if ( !segmented ) bar.segments = 1;
		toggle_popup_row( "Show Value", bar.show_value, 1, [ & ]
			{
				color_row( "Text Color", bar.text_color );
			} );
		color_row( "Background Color", bar.background_color );
	}

	void player_weapon_settings_rows( config::visual_profile::player::weapon& weapon )
	{
		int display = static_cast<int>( weapon.display );
		static constexpr const char* displays[]{ "Text", "Icon", "Text + Icon" };
		select_row( "Display", display, displays );
		weapon.display = static_cast<config::visual_profile::player::weapon::display_type>( display );
		color_row( "Text Color", weapon.text_color );
		color_row( "Icon Color", weapon.icon_color );
		toggle_popup_row( "Ammo Indicator", weapon.ammo.enabled, 2, [ & ]
			{
				toggle_row( "Exact Ammo Count", weapon.ammo.show_count );
				color_row( "Empty Color", weapon.ammo.empty_color );
			} );
	}

	config::visual_profile::player::info_flags::style& selected_info_flag_style(
		config::visual_profile::player::info_flags& flags, int selected )
	{
		switch ( std::clamp( selected, 0, 8 ) )
		{
		case 0: return flags.money_style;
		case 1: return flags.armor_style;
		case 2: return flags.kit_style;
		case 3: return flags.scoped_style;
		case 4: return flags.defusing_style;
		case 5: return flags.flashed_style;
		case 6: return flags.ping_style;
		case 7: return flags.distance_style;
		default: return flags.bomb_damage_style;
		}
	}

	void player_info_flag_settings_rows( config::visual_profile::player::info_flags& flags )
	{
		using flag = config::visual_profile::player::info_flags::flag;
		static constexpr std::pair<const char*, int> options[]{
			{ "Money", flag::money }, { "Armor", flag::armor }, { "Defuse Kit", flag::kit },
			{ "Scoped", flag::scoped }, { "Defusing", flag::defusing },
			{ "Flashed", flag::flashed }, { "Ping", flag::ping },
			{ "Distance", flag::distance }, { "Bomb Damage", flag::bomb_damage }
		};
		int mask = flags.flags;
		multiselect_row( "Active Flags", mask, options, ( 1 << 9 ) - 1 );
		flags.flags = static_cast<std::uint16_t>( mask );

		static int selected{};
		static constexpr const char* names[]{
			"Money", "Armor", "Defuse Kit", "Scoped", "Defusing",
			"Flashed", "Ping", "Distance", "Bomb Damage"
		};
		select_row( "Edit Flag", selected, names );
		auto& style = selected_info_flag_style( flags, selected );
		color_row( "Color", style.color );
		slider_row( "Scale", style.scale, 0.55f, 2.0f, "x", 0.05f );
	}

	template<typename callback_t>
	void visual_editor_settings_popup( int rows, callback_t&& callback )
	{
		const auto element_min = ImGui::GetItemRectMin( );
		const auto element_max = ImGui::GetItemRectMax( );
		if ( ImGui::IsItemHovered( ) && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
			ImGui::OpenPopup( "##element_settings" );
		const auto popup_id = ImGui::GetID( "##element_settings" );
		auto& alpha = anim_state<float>( popup_id );
		const auto open = ImGui::IsPopupOpen( "##element_settings" );
		ease_static( alpha, open ? 1.0f : 0.0f, 8.0f );
		if ( !open ) return;

		constexpr auto popup_width = 300.0f;
		const auto known = g_popup_heights.find( popup_id );
		const auto height = known != g_popup_heights.end( ) ? known->second : 50.0f + rows * 44.0f;
		const auto display = ImGui::GetIO( ).DisplaySize;
		const settings_bounds_scope popup_bounds{ { 0.0f, 0.0f }, display };
		const auto cursor = ImGui::GetMousePos( );
		auto position = ImVec2{ std::max( cursor.x + 10.0f, element_max.x + 10.0f ), cursor.y + 8.0f };
		if ( position.x + popup_width > display.x - 8.0f )
			position.x = std::min( cursor.x - popup_width - 10.0f, element_min.x - popup_width - 10.0f );
		position.x = std::clamp( position.x, 8.0f, std::max( 8.0f, display.x - popup_width - 8.0f ) );
		position.y = std::clamp( position.y, 8.0f, std::max( 8.0f, display.y - height - 8.0f ) );
		ImGui::SetNextWindowPos( position, ImGuiCond_Appearing );
		ImGui::SetNextWindowSize( { popup_width, 0.0f }, ImGuiCond_Always );
		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, ImGui::GetStyle( ).Alpha * std::max( alpha, 0.01f ) );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 10.0f, 10.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 8.0f, 4.0f } );
		ImGui::PushStyleVar( ImGuiStyleVar_PopupBorderSize, 0.0f );
		ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4{ 0, 0, 0, 0 } );
		if ( ImGui::BeginPopup( "##element_settings",
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground ) )
		{
			popup_surface( ui_label( "Settings" ), true );
			ImGui::SetCursorPosY( 36.0f );
			callback( );
			ImGui::Dummy( { popup_width - 20.0f, 6.0f } );
			g_popup_heights[ popup_id ] = ImGui::GetWindowSize( ).y;
			ImGui::EndPopup( );
		}
		ImGui::PopStyleColor( );
		ImGui::PopStyleVar( 4 );
	}

	// ------------------------------------------------------------------- childs
	// gui.cpp child: shadow, top->bottom gradient, 32px header with accent
	// title and a soft shadow under the header; scales in on tab change.
	constexpr float k_child_header = 32.0f;
	constexpr float k_child_spacing = 15.0f;
	constexpr float k_child_margin = 8.0f;
	ImVec2 g_cards_origin{};
	float g_cards_y[ 2 ]{};
	float g_cards_width{};
	int g_cards_index{};
	float g_tab_transition{ 1.0f };
	std::unordered_map<ImGuiID, float> g_card_heights{};

	// Like the framework's fixed 2x2 childs, columns stretch their cards to the
	// visible height and a page with a single column spans the full width.
	// Uses the previous frame's layout of the same card grid.
	struct cards_layout
	{
		float total[ 2 ]{};
		float tallest[ 2 ]{};
		int count[ 2 ]{};
	};
	std::unordered_map<ImGuiID, cards_layout> g_cards_layouts{};
	ImGuiID g_cards_id{};
	cards_layout g_cards_current{};
	float g_cards_extra[ 2 ]{};
	float g_cards_uniform[ 2 ]{};
	bool g_cards_single{};

	void draw_child_frame( ImDrawList* draw, ImVec2 min, ImVec2 max, std::string_view title, float t )
	{
		const auto header_top = with_alpha( k_bg_card, k_bg_card.w * t );
		const auto header_bottom = with_alpha( mix( k_bg_card, k_bg_base, 0.5f ), k_bg_card.w * t );
		const auto bg_bottom = with_alpha( k_bg_base, k_bg_card.w * t );
		layered_shadow( draw, min, max, 5, 8.0f, 0.25f * t, 8.0f );
		draw->PushClipRect( min, max, true );
		draw->AddRectFilled( min, max, clr( bg_bottom ), 8.0f );
		vertical_gradient( draw, min, max, clr( header_top ), clr( bg_bottom ) );
		draw->PopClipRect( );

		const auto header_max = ImVec2{ max.x, min.y + k_child_header };
		for ( int hs = 0; hs < 4; ++hs )
		{
			const auto progress = hs / 4.0f;
			const auto blur = 6.0f * progress;
			draw->AddRectFilled( min - ImVec2{ blur, blur }, header_max + ImVec2{ blur, blur },
				clr( { 0, 0, 0, ( 1.0f - progress ) * 0.2f * t } ), 8.0f + blur, ImDrawFlags_RoundCornersTop );
		}
		draw->PushClipRect( min, header_max, true );
		draw->AddRectFilled( min, header_max, clr( header_bottom ), 8.0f, ImDrawFlags_RoundCornersTop );
		vertical_gradient( draw, min, header_max, clr( header_top ), clr( header_bottom ) );
		draw->PopClipRect( );

		const auto size = text_size( 15.0f, title );
		text( draw, 15.0f, { min.x + 15.0f, min.y + ( k_child_header - size.y ) * 0.5f }, clr( k_accent, t ), title );

		for ( int bs = 0; bs < 5; ++bs )
		{
			const auto progress = bs / 5.0f;
			const auto alpha = ( 1.0f - progress ) * 0.35f * t;
			draw->AddRectFilledMultiColor( { min.x, header_max.y + 10.0f * progress },
				{ max.x, header_max.y + 10.0f * ( progress + 0.2f ) },
				clr( { 0, 0, 0, alpha } ), clr( { 0, 0, 0, alpha } ), 0, 0 );
		}
	}

	template<typename callback_t>
	void card_in_column( const char* id, const char* title, int rows, int column, callback_t&& callback )
	{
		column = std::clamp( column, 0, 1 );
		const auto card_id = ImGui::GetID( id );
		const auto card_width = g_cards_single ? g_cards_width * 2.0f + k_child_spacing : g_cards_width;
		const auto card_min = ImVec2{ g_cards_origin.x + ( g_cards_single ? 0.0f : column * ( g_cards_width + k_child_spacing ) ),
			g_cards_origin.y + g_cards_y[ column ] };
		auto& natural = g_card_heights[ card_id ];
		if ( natural <= 0.0f ) natural = k_child_header + 15.0f + rows * 40.0f + 12.0f;
		const auto height = g_cards_uniform[ column ] > 0.0f ? g_cards_uniform[ column ] : natural + g_cards_extra[ column ];
		const auto card_max = card_min + ImVec2{ card_width, height };

		const auto t = std::clamp( g_tab_transition, 0.0f, 1.0f );
		const auto scale = 0.85f + t * t * 0.15f;
		const auto center = ( card_min + card_max ) * 0.5f;
		const auto scaled_min = center + ( card_min - center ) * scale;
		const auto scaled_max = center + ( card_max - center ) * scale;
		draw_child_frame( ImGui::GetWindowDrawList( ), scaled_min, scaled_max, ui_label( title ), t );

		ImGui::SetCursorScreenPos( card_min + ImVec2{ 0.0f, k_child_header + 15.0f } + ( scaled_min - card_min ) );
		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, ImGui::GetStyle( ).Alpha * t );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
		ImGui::BeginChild( id, { card_width, std::max( 1.0f, height - k_child_header - 15.0f ) }, false,
			ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 8.0f, 4.0f } );
		ImGui::Indent( 12.0f );
		callback( );
		ImGui::Unindent( 12.0f );
		const auto content = ImGui::GetCursorPosY( );
		ImGui::PopStyleVar( );
		ImGui::EndChild( );
		ImGui::PopStyleVar( 2 );
		natural = k_child_header + 15.0f + std::max( 20.0f, content ) + 12.0f;
		g_cards_current.total[ column ] += natural + ( g_cards_current.count[ column ] ? k_child_spacing : 0.0f );
		g_cards_current.tallest[ column ] = std::max( g_cards_current.tallest[ column ], natural );
		++g_cards_current.count[ column ];
		g_cards_y[ column ] += height + k_child_spacing;
	}

	template<typename callback_t>
	void card( const char* id, const char* title, int rows, callback_t&& callback )
	{
		card_in_column( id, title, rows, g_cards_index++ % 2, std::forward<callback_t>( callback ) );
	}

	void begin_cards( const char* id )
	{
		ImGui::PushID( id );
		g_cards_origin = ImGui::GetCursorScreenPos( ) + ImVec2{ k_child_margin, k_child_margin };
		g_cards_width = ( ImGui::GetContentRegionAvail( ).x - k_child_margin * 2.0f - k_child_spacing ) * 0.5f;
		g_cards_y[ 0 ] = g_cards_y[ 1 ] = 0.0f;
		g_cards_index = 0;
		g_cards_id = ImGui::GetID( "##layout" );
		g_cards_current = {};
		const auto visible = ImGui::GetContentRegionAvail( ).y - k_child_margin * 2.0f;
		const auto previous = g_cards_layouts.find( g_cards_id );
		g_cards_single = false;
		g_cards_extra[ 0 ] = g_cards_extra[ 1 ] = 0.0f;
		g_cards_uniform[ 0 ] = g_cards_uniform[ 1 ] = 0.0f;
		if ( previous != g_cards_layouts.end( ) )
		{
			const auto& layout = previous->second;
			g_cards_single = layout.count[ 0 ] > 0 && layout.count[ 1 ] == 0;
			// Equal rows (the framework's 2x2 grid) when both columns hold the same
			// number of cards and every card fits its share; otherwise stretch.
			const auto rows = std::max( layout.count[ 0 ], layout.count[ 1 ] );
			const auto share = rows > 0 ? ( visible - k_child_spacing * ( rows - 1 ) ) / rows : 0.0f;
			const auto uniform = !g_cards_single && layout.count[ 0 ] == layout.count[ 1 ]
				&& std::max( layout.tallest[ 0 ], layout.tallest[ 1 ] ) <= share;
			for ( int column = 0; column < 2; ++column )
			{
				if ( layout.count[ column ] <= 0 ) continue;
				if ( uniform ) g_cards_uniform[ column ] = share;
				else g_cards_extra[ column ] = std::max( 0.0f, ( visible - layout.total[ column ] ) / layout.count[ column ] );
			}
		}
	}

	void end_cards( )
	{
		g_cards_layouts[ g_cards_id ] = g_cards_current;
		const auto content_height = std::max( g_cards_y[ 0 ], g_cards_y[ 1 ] );
		ImGui::SetCursorScreenPos( g_cards_origin - ImVec2{ k_child_margin, k_child_margin } );
		ImGui::Dummy( { g_cards_width * 2.0f + k_child_spacing + k_child_margin * 2.0f,
			std::max( 0.0f, content_height - k_child_spacing + k_child_margin * 2.0f ) } );
		ImGui::PopID( );
	}

	// -------------------------------------------------------------------- tabbar
	constexpr float k_tabbar_height = 45.0f;
	constexpr float k_content_padding = 20.0f;
	constexpr float k_content_top = 25.0f;
	const ImVec4 k_tab_inactive{ 120.0f / 255.0f, 120.0f / 255.0f, 125.0f / 255.0f, 1.0f };
	const ImVec4 k_tab_hover{ 140.0f / 255.0f, 140.0f / 255.0f, 145.0f / 255.0f, 1.0f };

	// Icon / text box used by the tabbar (gui.cpp tab boxes).
	bool tab_box( const char* id, ImRect box, std::string_view label, float size, bool active, bool icon )
	{
		ImGui::SetCursorScreenPos( box.Min );
		const auto clicked = ImGui::InvisibleButton( id, box.GetSize( ) );
		const auto hovered = ImGui::IsItemHovered( );
		pointer_cursor_if_hovered( );
		auto& hover = anim_state<float>( ImGui::GetItemID( ) );
		ease_dynamic( hover, active ? 2.0f : hovered ? 1.0f : 0.0f, 14.0f );

		auto* draw = ImGui::GetWindowDrawList( );
		layered_shadow( draw, box.Min, box.Max, 5, 6.0f, 0.25f, 6.0f );
		draw->PushClipRect( box.Min, box.Max, true );
		draw->AddRectFilled( box.Min, box.Max, clr_rgb( 12, 12, 16 ), 6.0f );
		vertical_gradient( draw, box.Min, box.Max, clr( with_alpha( k_bg_card, 1.0f ) ), clr_rgb( 12, 12, 16 ) );
		draw->PopClipRect( );

		const auto color = hover <= 1.0f ? mix( k_tab_inactive, k_tab_hover, hover ) : mix( k_tab_hover, k_accent, hover - 1.0f );
		const auto extent = text_size( size, label );
		const auto position = ImVec2{ box.Min.x + ( box.GetWidth( ) - extent.x ) * 0.5f,
			box.Min.y + ( box.GetHeight( ) - extent.y ) * 0.5f + ( icon ? 1.0f : 0.0f ) };
		if ( active )
		{
			for ( int g = 2; g > 0; --g )
			{
				const auto progress = g / 2.0f;
				const auto alpha = ( 1.0f - progress ) * 0.25f;
				const auto offset = progress * 1.5f;
				const ImVec2 offsets[]{ { -offset, 0 }, { offset, 0 }, { 0, -offset }, { 0, offset } };
				for ( const auto& o : offsets ) text( draw, size, position + o, clr( k_accent, alpha ), label );
			}
		}
		draw->PushClipRect( box.Min, box.Max, true );
		text( draw, size, position, clr( color ), label );
		draw->PopClipRect( );
		return clicked;
	}

	constexpr const char* k_chams_materials[]
	{
		"Solid", "Shaded", "Glow", "Glow Outline", "Iridescent", "Water Flow", "Glossy"
	};
	static_assert( std::size( k_chams_materials ) == config::visual_profile::chams::material_type_count );

	[[nodiscard]] int chams_material_row_count( const config::visual_profile::chams::material& m )
	{
		constexpr auto common = 3;
		switch ( m.type )
		{
		case config::visual_profile::chams::shaded:       return common + 2;
		case config::visual_profile::chams::glow:         return common + 1;
		case config::visual_profile::chams::glow_outline: return common + 3;
		case config::visual_profile::chams::iridescent:   return common + 2;
		case config::visual_profile::chams::water_flow:   return common + 1;
		case config::visual_profile::chams::glossy:       return common + 4;
		default:                                 return common;
		}
	}

	void chams_material_rows( config::visual_profile::chams::material& m )
	{
		select_row( "Material", m.type, k_chams_materials );
		color_row( "Color", m.color );
		toggle_row( "Wireframe", m.wireframe );

		switch ( m.type )
		{
		case config::visual_profile::chams::shaded:
			slider_row( "Roughness", m.roughness, 0.0f, 1.0f, "", 0.01f );
			slider_row( "Metalness", m.metalness, 0.0f, 1.0f, "", 0.01f );
			break;

		case config::visual_profile::chams::glow:
			slider_row( "Exponent", m.exponent, 0.1f, 8.0f, "", 0.1f );
			break;

		case config::visual_profile::chams::glow_outline:
			slider_row( "Exponent", m.exponent, 0.1f, 8.0f, "", 0.1f );
			slider_row( "Falloff", m.falloff, 0.05f, 2.0f, "", 0.05f );
			slider_row( "Fresnel Filling", m.fresnel_fill, 0.0f, 1.0f, "", 0.01f );
			break;

		case config::visual_profile::chams::iridescent:
			slider_row( "Strength", m.strength, 0.0f, 1.0f, "", 0.01f );
			slider_row( "Roughness", m.roughness, 0.0f, 1.0f, "", 0.01f );
			break;

		case config::visual_profile::chams::water_flow:
			slider_row( "Speed", m.speed, 0.0f, 4.0f, "", 0.05f );
			break;

		case config::visual_profile::chams::glossy:
			slider_row( "Exponent", m.exponent, 0.1f, 8.0f, "", 0.1f );
			slider_row( "Fresnel Filling", m.fresnel_fill, 0.0f, 1.0f, "", 0.01f );
			slider_row( "Falloff", m.falloff, 0.05f, 2.0f, "", 0.05f );
			color_row( "Tint", m.tint );
			break;

		default:
			break;
		}
	}

}

bool menu_t::initialize( HWND hwnd )
{
	this->m_hwnd = hwnd;
	return true;
}

void menu_t::map_pointer_to_layout(
	float& x, float& y, const float display_width,
	const float display_height ) const noexcept
{
	if ( !this->is_open( ) ) return;
	const auto scale = current_menu_scale( display_width, display_height );
	if ( std::abs( scale - 1.0f ) < 0.0001f ) return;
	const auto origin = menu_transform_origin( display_width, display_height );
	x = origin.x + ( x - origin.x ) / scale;
	y = origin.y + ( y - origin.y ) / scale;
}

void menu_t::map_pointer_to_screen(
	float& x, float& y, const float display_width,
	const float display_height ) const noexcept
{
	if ( !this->is_open( ) ) return;
	const auto scale = current_menu_scale( display_width, display_height );
	if ( std::abs( scale - 1.0f ) < 0.0001f ) return;
	const auto origin = menu_transform_origin( display_width, display_height );
	x = origin.x + ( x - origin.x ) * scale;
	y = origin.y + ( y - origin.y ) * scale;
}

bool menu_t::binding_key( ) const
{
	return this->is_open( ) && g_listening_key != nullptr
		&& ImGui::GetCurrentContext( ) && g_listening_seen_frame == ImGui::GetFrameCount( );
}

void menu_t::poll_hotkey( )
{
	if ( !this->binding_key( ) ) g_listening_key = nullptr;
	
	// Get the menu key
	const auto menu_key = platform::windows::lifecycle_keys( ).menu;
	
	// Check for both configured menu key and DEL key as alternatives
	const bool primary_key_down = ( ::GetAsyncKeyState( menu_key ) & 0x8000 ) != 0;
	const bool del_key_down = ( ::GetAsyncKeyState( VK_DELETE ) & 0x8000 ) != 0;
	const bool hotkey_is_down = primary_key_down || del_key_down;
	
	// Edge detection: detect key press (not release or repeat)
	const bool key_just_pressed = hotkey_is_down && !this->m_menu_hotkey_was_down;
	this->m_menu_hotkey_was_down = hotkey_is_down;
	
	// Handle key press while not binding
	if ( key_just_pressed && !this->binding_key( ) )
	{
		// Prevent rapid toggling (minimum 100ms between toggles)
		const auto now = std::chrono::steady_clock::now();
		const auto time_since_last_toggle = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - this->m_last_toggle_time).count();
		
		if ( time_since_last_toggle < 100 )
			return; // Ignore if too soon after last toggle
		
		this->m_last_toggle_time = now;
		
		// If menu is currently open, close it immediately
		if ( this->m_open.load( std::memory_order_acquire ) )
		{
			this->m_open.store( false, std::memory_order_release );
			this->m_open_pending = false;
			return; // Exit early after closing
		}
		
		// If menu is closed, request to open it
		this->m_open_pending = true;
	}
	
	// If not pending open, nothing more to do
	if ( !this->m_open_pending )
		return;
	
	// If already open (race condition), clear pending and exit
	if ( this->m_open.load( std::memory_order_acquire ) )
	{
		this->m_open_pending = false;
		return;
	}
	
	// Check if movement keys are pressed - if so, wait
	std::vector<std::uint16_t> movement_keys{ 'W', 'A', 'S', 'D' };
	constexpr std::array movement_actions{
		game::input_action::forward, game::input_action::back,
		game::input_action::left, game::input_action::right,
	};
	for ( const auto action : movement_actions )
	{
		for ( const auto& binding : game::input_bindings( ).candidates( action ) )
		{
			if ( binding.device == game::input_device::keyboard && binding.virtual_key
				&& std::ranges::find( movement_keys, binding.virtual_key )
					== movement_keys.end( ) )
			{
				movement_keys.push_back( binding.virtual_key );
			}
		}
	}
	if ( std::ranges::any_of( movement_keys, [ ]( const std::uint16_t key )
		{
			return app::context( ).input.physical_key_down( key );
		} ) )
	{
		return; // Wait for movement keys to be released
	}

	// All conditions met - open the menu
	this->m_open.store( true, std::memory_order_release );
	this->m_open_pending = false;
	
	// Release any held movement keys
	std::vector<platform::windows::input_gateway::key_transition> releases{};
	releases.reserve( movement_keys.size( ) );
	for ( const auto key : movement_keys ) releases.push_back( { key, false } );
	app::context( ).input.keys( releases );
	app::context( ).input.set_movement_gate( {}, false );
	
	this->reset_content_animation( );
}

void menu_t::reset_content_animation( )
{
	this->m_menu_alpha = 0.0f;
	this->m_tab_transition = 0.0f;
}

void menu_t::select_tab( int tab )
{
	tab = std::clamp( tab, 0, 5 );
	if ( tab == this->active_tab( ) ) return;
	if ( tab < 3 ) this->m_page = tab;
	else
	{
		this->m_page = 3;
		this->m_misc_group = tab == 4 ? 5 : tab == 5 ? 3 : this->m_last_misc_group;
	}
	this->m_tab_transition = 0.0f;
}

int menu_t::active_tab( ) const noexcept
{
	if ( this->m_page < 3 ) return std::max( this->m_page, 0 );
	if ( this->m_misc_group == 5 ) return 4;
	if ( this->m_misc_group == 3 ) return 5;
	return 3;
}

void menu_t::draw( )
{
	if ( !this->m_open.load( std::memory_order_acquire ) )
	{
		return;
	}
	synchronize_menu_palette( );

	const auto display = ImGui::GetIO( ).DisplaySize;
	const auto menu_scale = current_menu_scale( display.x, display.y );
	const auto transform_origin = menu_transform_origin( display.x, display.y );
	const menu_render_scope render_scope{ display, transform_origin, menu_scale };
	begin_menu_foreground( );
	const auto initial_position = initial_menu_layout_position( display.x, display.y );
	ImGui::SetNextWindowPos( initial_position, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( { k_menu_width, k_menu_height }, ImGuiCond_Always );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, k_menu_rounding );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarSize, 6.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarRounding, 4.0f );
	ImGui::PushStyleColor( ImGuiCol_Text, k_text_main );
	ImGui::PushStyleColor( ImGuiCol_ScrollbarBg, clr_rgb( 45, 45, 50 ) );
	ImGui::PushStyleColor( ImGuiCol_ScrollbarGrab, k_accent );
	ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabHovered, mix( k_accent, ImVec4{ 1, 1, 1, 1 }, 0.08f ) );
	ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabActive, mix( k_accent, ImVec4{ 0, 0, 0, 1 }, 0.08f ) );
	ImGui::PushFont( munchi_font( ), 13.0f );
	ImGui::Begin( "##vesta_native_menu", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse );

	ease_dynamic( this->m_menu_alpha, 1.0f, 6.0f );
	ease_dynamic( this->m_tab_transition, 1.0f, 6.0f );
	g_tab_transition = this->m_tab_transition;
	const auto open_alpha = this->m_menu_alpha;
	const auto window_pos = ImGui::GetWindowPos( );
	const auto window_size = ImVec2{ k_menu_width, k_menu_height };
	g_menu_min = window_pos;
	g_menu_max = window_pos + window_size;

	// Open animation: the window body scales up from 85% (gui.cpp).
	const auto scale = 0.85f + open_alpha * open_alpha * 0.15f;
	const auto center = window_pos + window_size * 0.5f;
	const auto pos = center + ( window_pos - center ) * scale;
	const auto size = window_size * scale;
	const auto max = pos + size;
	auto* draw = ImGui::GetWindowDrawList( );

	draw->PushClipRectFullScreen( );
	for ( int s = 0; s < 6; ++s )
	{
		const auto progress = s / 6.0f;
		const auto blur = 12.0f * progress;
		draw->AddRectFilled( pos - ImVec2{ blur, blur }, max + ImVec2{ blur, blur },
			IM_COL32( 0, 0, 0, static_cast<int>( ( 1.0f - progress ) * 0.35f * open_alpha * 255.0f ) ),
			k_menu_rounding + blur );
	}
	draw->PopClipRect( );
	draw->PushClipRect( pos, max, true );
	const auto bg_top = with_alpha( k_bg_card, k_bg_base.w * open_alpha );
	const auto bg_bottom = with_alpha( k_bg_base, k_bg_base.w * open_alpha );
	draw->AddRectFilled( pos, max, packed( bg_bottom ), k_menu_rounding );
	draw->AddRectFilledMultiColor( pos, max, packed( bg_top ), packed( bg_top ), packed( bg_bottom ), packed( bg_bottom ) );
	{
		// Style alpha is still 1 here, so inset_blur's clr( ) matches packed( ).
		inset_blur( draw, pos, max, k_menu_rounding, 16, 0.8f, 0.06f, 45.0f, 12.0f );
	}
	draw->PopClipRect( );
	draw->AddRect( pos, max, packed( with_alpha( k_accent, 0.15f * open_alpha ) ), k_menu_rounding, 0, 1.5f );

	ImGui::PushStyleVar( ImGuiStyleVar_Alpha, open_alpha );
	this->draw_tabbar( pos, size );
	this->draw_content( );
	this->draw_esp_toggle( pos, size );
	ImGui::PopStyleVar( );

	ImGui::End( );
	this->draw_visual_editor( );
	ImGui::PopFont( );
	end_menu_foreground( );
	scale_menu_draw_lists( transform_origin, menu_scale );
	ImGui::PopStyleColor( 5 );
	ImGui::PopStyleVar( 5 );
}

void menu_t::draw_tabbar( ImVec2 pos, ImVec2 size )
{
	auto* draw = ImGui::GetWindowDrawList( );
	const auto tab_start = pos;
	const auto tab_end = ImVec2{ pos.x + size.x, pos.y + k_tabbar_height };
	draw->PushClipRect( tab_start, tab_end, true );
	const auto tab_bottom = clr_rgb( 14, 14, 18 );
	draw->AddRectFilled( tab_start, tab_end, tab_bottom, k_menu_rounding, ImDrawFlags_RoundCornersTop );
	vertical_gradient( draw, tab_start, tab_end, clr( with_alpha( k_bg_card, 1.0f ) ), tab_bottom );
	draw->PopClipRect( );

	// Logo with glow, drop shadow and a highlight over its upper third.
	constexpr std::string_view logo{ "vesta" };
	const auto logo_size = text_size( 18.0f, logo );
	const auto logo_pos = ImVec2{ tab_start.x + 20.0f, tab_start.y + ( k_tabbar_height - logo_size.y ) * 0.5f };
	const auto open_alpha = ImGui::GetStyle( ).Alpha;
	{
		// Ten glow passes at the same position == one pass with their combined alpha.
		auto keep = 1.0f;
		for ( int g = 10; g > 0; --g ) keep *= 1.0f - ( g / 10.0f ) * 0.4f * open_alpha;
		text( draw, 18.0f, logo_pos, packed( with_alpha( k_accent, 1.0f - keep ) ), logo );
	}
	text( draw, 18.0f, logo_pos + ImVec2{ 1.0f, 1.0f }, IM_COL32( 0, 0, 0, static_cast<int>( 100 * open_alpha ) ), logo );
	text( draw, 18.0f, logo_pos, packed( with_alpha( k_accent, open_alpha ) ), logo );
	draw->PushClipRect( logo_pos, logo_pos + ImVec2{ logo_size.x, logo_size.y * 0.3f }, true );
	text( draw, 18.0f, logo_pos, packed( with_alpha( mix( k_accent, ImVec4{ 1, 1, 1, 1 }, 0.35f ), 0.5f * open_alpha ) ), logo );
	draw->PopClipRect( );

	// Icon tabs (IconsV2): aimbot, triggerbot, visuals, misc, configs, settings.
	static constexpr const char* icons[]{
		"\xEF\x80\x98", "\xEF\x81\xA3", "\xEF\x81\x8B", "\xEF\x80\x8C", "\xEF\x80\xAF", "\xEF\x80\x8D" };
	constexpr int tab_count = 6;
	constexpr auto box_size = 32.0f;
	constexpr auto tab_spacing = 8.0f;
	const auto total = tab_count * box_size + ( tab_count - 1 ) * tab_spacing;
	const auto tabs_x = pos.x + size.x - total - 15.0f;
	const auto active = this->active_tab( );
	for ( int i = 0; i < tab_count; ++i )
	{
		const auto x = tabs_x + i * ( box_size + tab_spacing );
		const ImRect box{ { x, pos.y + ( k_tabbar_height - box_size ) * 0.5f },
			{ x + box_size, pos.y + ( k_tabbar_height + box_size ) * 0.5f } };
		ImGui::PushID( i );
		if ( tab_box( "##tab", box, icons[ i ], 16.0f, i == active, true ) ) this->select_tab( i );
		ImGui::PopID( );
	}

	// Category boxes between the logo and the icon tabs.
	static constexpr const char* combat[]{ "Global", "Pistol", "SMG", "Rifle", "Shotgun", "Sniper", "Heavy" };
	static constexpr const char* visuals[]{ "Players", "Items", "Projectiles", "Bomb", "Radar", "Effects", "Crosshair" };
	static constexpr std::pair<const char*, int> misc[]{ { "General", 0 }, { "Grenades", 1 }, { "Movement", 2 }, { "Lua API", 4 } };
	std::vector<std::pair<std::string, int>> items{};
	int selected{ -1 };
	if ( active < 2 )
	{
		for ( int i = 0; i < 7; ++i ) items.emplace_back( ui_label( combat[ i ] ), i - 1 );
		selected = this->m_weapon_group;
	}
	else if ( active == 2 )
	{
		for ( int i = 0; i < 7; ++i ) items.emplace_back( ui_label( visuals[ i ] ), i );
		selected = this->m_visual_group;
	}
	else if ( active == 3 )
	{
		for ( const auto& [ name, group ] : misc ) items.emplace_back( ui_label( name ), group );
		selected = this->m_misc_group;
	}
	if ( items.empty( ) ) return;

	const auto start_x = logo_pos.x + logo_size.x + 28.0f;
	const auto available = tabs_x - 20.0f - start_x;
	auto font_size = 12.0f;
	auto padding = 12.0f;
	constexpr auto gap = 6.0f;
	const auto measure = [ & ]
	{
		auto width = gap * ( items.size( ) - 1 );
		for ( const auto& [ label, value ] : items ) width += text_size( font_size, label ).x + padding * 2.0f;
		return width;
	};
	while ( measure( ) > available && ( padding > 6.0f || font_size > 10.0f ) )
	{
		if ( padding > 6.0f ) padding -= 1.0f;
		else font_size -= 0.5f;
	}
	constexpr auto box_height = 26.0f;
	auto x = start_x;
	for ( const auto& [ label, value ] : items )
	{
		const auto width = text_size( font_size, label ).x + padding * 2.0f;
		const ImRect box{ { x, pos.y + ( k_tabbar_height - box_height ) * 0.5f },
			{ x + width, pos.y + ( k_tabbar_height + box_height ) * 0.5f } };
		ImGui::PushID( value + 16 );
		if ( tab_box( "##category", box, label, font_size, value == selected, false ) && value != selected )
		{
			if ( active < 2 ) this->m_weapon_group = value;
			else if ( active == 2 ) this->m_visual_group = value;
			else { this->m_misc_group = value; this->m_last_misc_group = value; }
			this->m_tab_transition = 0.0f;
		}
		ImGui::PopID( );
		x += width + gap;
	}
}

void menu_t::draw_content( )
{
	auto* draw = ImGui::GetWindowDrawList( );
	const auto window_pos = ImGui::GetWindowPos( );
	const auto open_alpha = ImGui::GetStyle( ).Alpha;

	// Soft shadow cast by the tabbar onto the content (12 layers, 20px).
	for ( int i = 0; i < 12; ++i )
	{
		const auto p0 = i / 12.0f;
		const auto p1 = ( i + 1 ) / 12.0f;
		const auto a0 = ( 1.0f - p0 ) * ( 1.0f - p0 ) * 0.6f * open_alpha;
		const auto a1 = ( 1.0f - p1 ) * ( 1.0f - p1 ) * 0.6f * open_alpha;
		draw->AddRectFilledMultiColor( { window_pos.x, window_pos.y + k_tabbar_height + 20.0f * p0 },
			{ window_pos.x + k_menu_width, window_pos.y + k_tabbar_height + 20.0f * p1 },
			packed( { 0, 0, 0, a0 } ), packed( { 0, 0, 0, a0 } ), packed( { 0, 0, 0, a1 } ), packed( { 0, 0, 0, a1 } ) );
	}

	const auto content_min = ImVec2{ k_content_padding - k_child_margin, k_tabbar_height + k_content_top - k_child_margin };
	const auto content_size = ImVec2{ k_menu_width - k_content_padding * 2.0f + k_child_margin * 2.0f,
		k_menu_height - k_tabbar_height - k_content_top - k_content_padding + k_child_margin * 2.0f };
	ImGui::SetCursorPos( content_min );
	ImGui::BeginChild( "##content", content_size, false,
		ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	const auto tab = this->active_tab( );
	if ( tab == 0 ) this->draw_combat( false );
	else if ( tab == 1 ) this->draw_combat( true );
	else if ( tab == 2 ) this->draw_visuals( );
	else this->draw_misc( );
	ImGui::EndChild( );
}

// Visuals tab: side button that opens the ESP editor next to the menu.
void menu_t::draw_esp_toggle( ImVec2 pos, ImVec2 size )
{
	const auto visuals = this->active_tab( ) == 2;
	auto& reveal = anim_state<float>( ImGui::GetID( "##esp_toggle_reveal" ) );
	ease_dynamic( reveal, visuals ? 1.0f : 0.0f, 12.0f );
	if ( reveal < 0.01f ) return;

	constexpr auto width = 18.0f;
	constexpr auto height = 40.0f;
	const auto content_height = k_menu_height - k_tabbar_height - k_content_top - k_content_padding;
	const ImRect rect{ { pos.x + size.x, pos.y + k_tabbar_height + ( content_height - height ) * 0.5f },
		{ pos.x + size.x + width, pos.y + k_tabbar_height + ( content_height + height ) * 0.5f } };
	const auto hovered = visuals && ImGui::IsMouseHoveringRect( rect.Min, rect.Max, false );
	if ( hovered )
	{
		ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
		if ( ImGui::IsMouseClicked( ImGuiMouseButton_Left, false ) ) this->m_esp_preview_open = !this->m_esp_preview_open;
	}
	ease_static( this->m_esp_toggle_hover, hovered ? 1.0f : 0.0f, 20.0f );

	auto* draw = menu_foreground( );
	const auto alpha = reveal * ImGui::GetStyle( ).Alpha;
	for ( int s = 0; s < 5; ++s )
	{
		const auto progress = s / 5.0f;
		const auto blur = 6.0f * progress;
		draw->AddRectFilled( rect.Min - ImVec2{ blur, blur }, rect.Max + ImVec2{ blur, blur },
			packed( { 0, 0, 0, ( 1.0f - progress ) * 0.25f * alpha } ), 6.0f + blur );
	}
	draw->AddRectFilled( rect.Min, rect.Max, packed( { 40.0f / 255.0f, 40.0f / 255.0f, 46.0f / 255.0f, alpha } ), 6.0f );
	if ( this->m_esp_toggle_hover > 0.01f || this->m_esp_preview_open )
	{
		const auto effect = this->m_esp_preview_open ? 1.0f : this->m_esp_toggle_hover;
		draw->AddRectFilled( rect.Min, rect.Max, packed( with_alpha( k_accent, 0.25f * effect * alpha ) ), 6.0f );
	}
	constexpr std::string_view icon{ "\xEF\x81\x9F" };
	const auto extent = text_size( 14.0f, icon );
	const auto color = this->m_esp_preview_open ? with_alpha( k_accent, alpha )
		: hovered ? ImVec4{ 0.85f, 0.85f, 0.9f, alpha } : ImVec4{ 0.7f, 0.7f, 0.75f, alpha };
	text( draw, 14.0f, rect.Min + ( rect.GetSize( ) - extent ) * 0.5f, packed( color ), icon );
}

void menu_t::draw_visual_editor( )
{
	const auto requested = this->active_tab( ) == 2 && this->m_esp_preview_open;
	ease_dynamic( this->m_visual_editor_animation, requested ? 1.0f : 0.0f, 8.0f );
	if ( this->m_visual_editor_animation <= 0.01f ) return;

	const auto reveal = std::clamp( this->m_visual_editor_animation, 0.0f, 1.0f );
	const auto content_height = k_menu_height - k_tabbar_height - k_content_top - k_content_padding;
	const auto panel_size = ImVec2{ 320.0f, content_height };
	auto display = ImGui::GetIO( ).DisplaySize;
	this->map_pointer_to_layout(
		display.x, display.y, display.x, display.y );
	auto panel_x = g_menu_max.x + 18.0f + 15.0f;
	if ( panel_x + panel_size.x > display.x - 8.0f ) panel_x = std::max( 8.0f, display.x - panel_size.x - 8.0f );
	const auto panel_position = ImVec2{ panel_x, g_menu_min.y + k_tabbar_height };

	ImGui::SetNextWindowPos( panel_position, ImGuiCond_Always );
	ImGui::SetNextWindowSize( panel_size, ImGuiCond_Always );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 8.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_Alpha, reveal );
	ImGui::Begin( "##esp_visual_editor", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar );

	const auto panel_min = ImGui::GetWindowPos( );
	const auto panel_max = panel_min + panel_size;
	auto* draw = ImGui::GetWindowDrawList( );
	{
		// gui.cpp "3d esp preview" box: scale-in, shadow, gradient, inset blur, accent border.
		const auto scale = 0.85f + reveal * 0.15f;
		const auto center = ( panel_min + panel_max ) * 0.5f;
		const auto scaled_min = center + ( panel_min - center ) * scale;
		const auto scaled_max = center + ( panel_max - center ) * scale;
		draw->PushClipRectFullScreen( );
		for ( int s = 0; s < 6; ++s )
		{
			const auto progress = s / 6.0f;
			const auto blur = 12.0f * progress;
			draw->AddRectFilled( scaled_min - ImVec2{ blur, blur }, scaled_max + ImVec2{ blur, blur },
				packed( { 0, 0, 0, ( 1.0f - progress ) * 0.25f * reveal } ), 8.0f + blur );
		}
		draw->PopClipRect( );
		draw->PushClipRect( scaled_min, scaled_max, true );
		draw->AddRectFilledMultiColor( scaled_min, scaled_max,
			packed( with_alpha( k_bg_card, 0.95f * reveal ) ), packed( with_alpha( k_bg_card, 0.95f * reveal ) ),
			packed( with_alpha( k_bg_base, 0.95f * reveal ) ), packed( with_alpha( k_bg_base, 0.95f * reveal ) ) );
		inset_blur( draw, scaled_min, scaled_max, 8.0f, 16, 0.8f, 0.06f * reveal, 45.0f, 12.0f );
		draw->PopClipRect( );
		draw->AddRect( scaled_min, scaled_max, packed( with_alpha( k_accent, 0.15f * reveal ) ), 8.0f, 0, 1.5f );
		text( draw, 13.0f, scaled_min + ImVec2{ 15.0f, 15.0f }, packed( with_alpha( k_accent, reveal ) ), ui_label( "ESP Editor" ) );
	}

	const auto stage_min = panel_min + ImVec2{ 15.0f, 42.0f };
	const auto stage_max = panel_min + ImVec2{ panel_size.x - 15.0f, panel_size.y - 15.0f };

	const auto image_size = ImVec2{ 280.0f, 420.0f };

	constexpr auto composition_top = -16.0f;
	constexpr auto composition_bottom = 380.0f;
	const auto composition_height = composition_bottom - composition_top;
	const auto image_min = ImVec2{
		stage_min.x + ( stage_max.x - stage_min.x - image_size.x ) * 0.5f,
		stage_min.y + ( stage_max.y - stage_min.y - composition_height ) * 0.5f - composition_top };
	const auto image_max = image_min + image_size;

	const auto viewport_width = static_cast<std::uint32_t>( std::max( 16.0f, image_size.x ) );
	const auto viewport_height = static_cast<std::uint32_t>( std::max( 16.0f, image_size.y ) );

	auto& chams_cfg = config::visual_settings.m_chams;

	const config::visual_profile::chams::material* chams_material{};
	if ( chams_cfg.enabled )
	{
		if ( chams_cfg.visible.enabled ) chams_material = &chams_cfg.visible;
		else if ( chams_cfg.invisible.enabled ) chams_material = &chams_cfg.invisible;
	}

	const auto& seen_model = chams::g_renderer.diag( ).last_model_path;
	const auto model_path = seen_model.empty( )
		? std::string{ "agents/models/ctm_sas/ctm_sas.vmdl_c" }
		: seen_model;

	ID3D11ShaderResourceView* viewport_srv{};
	static_cast<void>( chams::g_renderer.ensure_vpk( ) );
	if ( chams::g_renderer.vpk_ready( ) )
	{
		viewport_srv = chams::g_preview.render( chams::g_renderer.vpk( ),
			model_path, viewport_width, viewport_height, chams_material );
	}

	const auto texture = viewport_srv ? viewport_srv : app::context().overlay.ct_preview_texture( );
	control_surface( draw, stage_min, stage_max, 6.0f );
	draw->AddRect( stage_min, stage_max, packed( { 0, 0, 0, 0.3f } ), 6.0f, 0, 1.0f );
	draw->PushClipRect( stage_min + ImVec2{ 1.0f, 1.0f }, stage_max - ImVec2{ 1.0f, 1.0f }, true );
	if ( texture )
	{
		const auto texture_id = static_cast<ImTextureID>( reinterpret_cast<std::uintptr_t>( texture ) );
		draw->AddImage( ImTextureRef{ texture_id }, image_min, image_max, { 0, 0 }, { 1, 1 }, IM_COL32_WHITE );
	}
	else
	{
		draw->AddText( image_min + ImVec2{ 72.0f, 190.0f }, packed( k_text_muted ), render::localization::tr( "PREVIEW UNAVAILABLE" ) );
	}

	auto& player = config::visual_settings.m_player;
	const auto point = [ & ]( float x, float y )
		{
			return image_min + ImVec2{ image_size.x * x, image_size.y * y };
		};
	const auto unit_min = point( 0.185f, 0.035f );
	const auto unit_max = point( 0.800f, 0.828f );
	const auto unit_size = unit_max - unit_min;

	if ( player.m_box.enabled )
	{
		zdraw::draw_list box_draw{ draw };
		const auto x = unit_min.x;
		const auto y = unit_min.y;
		const auto width = unit_size.x;
		const auto height = unit_size.y;
		if ( player.m_box.fill )
		{
			box_draw.add_rect_filled( x + 1.0f, y + 1.0f, width - 2.0f, height - 2.0f, { 60, 200, 100, 80 } );
		}
		if ( player.m_box.style == config::visual_profile::player::box::style_type::cornered )
		{
			const auto corner = std::min( player.m_box.corner_length, std::min( width, height ) * 0.4f );
			if ( player.m_box.outline )
			{
				box_draw.add_rect_cornered( x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f, { 0, 0, 0, 180 }, corner + 1.0f, 1.0f );
				box_draw.add_rect_cornered( x, y, width, height, { 0, 0, 0, 200 }, corner, 2.0f );
			}
			box_draw.add_rect_cornered( x, y, width, height, player.m_box.visible_color, corner, 1.0f );
		}
		else
		{
			if ( player.m_box.outline )
			{
				box_draw.add_rect( x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f, { 0, 0, 0, 180 }, 1.0f );
				box_draw.add_rect( x, y, width, height, { 0, 0, 0, 200 }, 2.0f );
			}
			box_draw.add_rect( x, y, width, height, player.m_box.visible_color, 1.0f );
		}
	}

	std::array<ImVec2, 24> skeleton_points{};
	for ( const auto bone : k_preview_skeleton_bones )
	{
		bool projected{};
		if ( viewport_srv )
		{
			foundation::vec3 world{};
			float x{}, y{};
			if ( chams::g_preview.bone_position( static_cast<std::uint32_t>( bone ), world )
				&& chams::g_preview.project( world, x, y ) )
			{
				skeleton_points[ bone ] = image_min + ImVec2{ x, y };
				projected = true;
			}
		}

		if ( !projected )
		{
			const auto normalized = k_preview_bones[ bone ];
			skeleton_points[ bone ] = point( normalized.x, normalized.y );
		}
	}

	const auto live_hitboxes = game::hitbox_data().snapshot( );

	const auto project_radius = [ & ]( const foundation::vec3& center, const foundation::vec3& axis,
		float radius, float cx, float cy ) -> float
		{
			const auto view_dir = ( center - chams::g_preview.eye( ) ).normalized( );
			auto perp = axis.cross( view_dir );
			if ( perp.length( ) < 0.001f )
			{
				perp = foundation::vec3{ 0.0f, 0.0f, 1.0f }.cross( view_dir );
				if ( perp.length( ) < 0.001f ) perp = foundation::vec3{ 1.0f, 0.0f, 0.0f };
			}
			perp.normalize( );

			float ex{}, ey{};
			if ( !chams::g_preview.project( center + perp * radius, ex, ey ) ) return 0.0f;
			return std::sqrt( ( ex - cx ) * ( ex - cx ) + ( ey - cy ) * ( ey - cy ) );
		};

	if ( player.m_threat_module.enabled )
	{
		const auto& threat = player.m_threat_module;
		const auto alpha_color = [ & ]( zdraw::rgba color, float alpha )
			{
				color.a = static_cast<std::uint8_t>( std::clamp( alpha, 0.0f, 255.0f ) );
				return packed( to_imvec( color ) );
			};
		const auto draw_capsule = [ & ]( ImVec2 from, ImVec2 to, float radius, const zdraw::rgba& color )
			{
				const auto outline_radius = radius + std::max( 1.0f, threat.outline_thickness );
				const auto draw_shape = [ & ]( float shape_radius, ImU32 shape_color )
				{
					const auto delta = to - from;
					const auto axis_length = std::sqrt( delta.x * delta.x + delta.y * delta.y );
					if ( axis_length < 1.0f )
					{
						draw->AddCircleFilled( from, shape_radius, shape_color, 24 );
						return;
					}

					constexpr auto segments = 12;
					constexpr auto pi = std::numbers::pi_v<float>;
					const auto phi = std::atan2( delta.y, delta.x );
					std::array<ImVec2, ( segments + 1 ) * 2> points{};
					int count{};
					for ( int segment = 0; segment <= segments; ++segment )
					{
						const auto angle = phi + pi * 0.5f + pi * static_cast<float>( segment ) / segments;
						points[ count++ ] = from + ImVec2{ std::cos( angle ), std::sin( angle ) } * shape_radius;
					}
					for ( int segment = 0; segment <= segments; ++segment )
					{
						const auto angle = phi - pi * 0.5f + pi * static_cast<float>( segment ) / segments;
						points[ count++ ] = to + ImVec2{ std::cos( angle ), std::sin( angle ) } * shape_radius;
					}
					draw->AddConvexPolyFilled( points.data( ), count, shape_color );
				};

				if ( threat.outline_alpha > 0.0f )
					draw_shape( outline_radius, alpha_color( color, threat.outline_alpha ) );
				if ( threat.fill_alpha > 0.0f )
					draw_shape( radius, alpha_color( color, threat.fill_alpha ) );
			};

		const auto hitbox_axis = [ & ]( int bone ) -> std::tuple<ImVec2, ImVec2, float>
			{
				if ( viewport_srv && live_hitboxes.count > 0 )
				{
					for ( const auto& hitbox : live_hitboxes )
					{
						if ( hitbox.bone != bone ) continue;

						foundation::vec3 from_world{}, to_world{};
						if ( !chams::g_preview.bone_transform( static_cast<std::uint32_t>( bone ), hitbox.mins, from_world )
							|| !chams::g_preview.bone_transform( static_cast<std::uint32_t>( bone ), hitbox.maxs, to_world ) )
						{
							break;
						}

						float ax{}, ay{}, bx{}, by{}, cx{}, cy{};
						const auto center_world = ( from_world + to_world ) * 0.5f;
						if ( !chams::g_preview.project( from_world, ax, ay )
							|| !chams::g_preview.project( to_world, bx, by )
							|| !chams::g_preview.project( center_world, cx, cy ) )
						{
							break;
						}

						const auto radius_px = project_radius( center_world, to_world - from_world,
							hitbox.radius, cx, cy );

						return { image_min + ImVec2{ ax, ay }, image_min + ImVec2{ bx, by }, radius_px };
					}
				}

				if ( viewport_srv && live_hitboxes.count > 0 ) return { {}, {}, 0.0f };

				const auto it = std::find_if( k_preview_hitboxes.begin( ), k_preview_hitboxes.end( ),
					[ & ]( const preview_hitbox_geometry& hitbox ) { return hitbox.bone == bone; } );
				if ( it == k_preview_hitboxes.end( ) ) return { {}, {}, 0.0f };
				return { point( it->from.x, it->from.y ), point( it->to.x, it->to.y ), it->radius };
			};
		const auto draw_hitbox = [ & ]( int bone, const zdraw::rgba& color )
			{
				const auto [ from, to, radius ] = hitbox_axis( bone );
				if ( radius > 0.0f ) draw_capsule( from, to, radius, color );
			};
		const auto draw_group = [ & ]( std::span<const int> bones, bool enabled, const zdraw::rgba& color )
			{
				if ( !enabled ) return;
				for ( const auto bone : bones ) draw_hitbox( bone, color );
			};

		draw_group( features::visuals::player_t::threat_head_bones, threat.head_hitbox, threat.head_color );
		draw_group( features::visuals::player_t::threat_body_bones, threat.body_hitbox, threat.body_color );
		draw_group( features::visuals::player_t::threat_limb_bones, threat.limb_hitbox, threat.limb_color );
	}
	if ( player.m_skeleton.enabled )
	{
		const auto skeleton_color = packed( to_imvec( player.m_skeleton.visible_color ) );
		for ( const auto& [ from, to ] : features::visuals::player_t::skeleton_connections )
		{
			draw->AddLine( skeleton_points[ from ], skeleton_points[ to ], skeleton_color, std::max( 1.0f, player.m_skeleton.thickness ) );
		}
	}
	if ( player.m_head_circle.enabled )
	{

		auto circle_center = skeleton_points[ 7 ];
		auto circle_radius = 19.5f;

		if ( viewport_srv )
		{
			for ( const auto& hitbox : live_hitboxes )
			{
				if ( game::hitbox_data().hitgroup_from_hitbox( hitbox.index ) != 1 ) continue;

				foundation::vec3 center_world{};
				const auto center_local = ( hitbox.mins + hitbox.maxs ) * 0.5f;
				float cx{}, cy{};
				if ( !chams::g_preview.bone_transform( static_cast<std::uint32_t>( hitbox.bone ), center_local, center_world )
							|| !chams::g_preview.project( center_world, cx, cy ) )
				{
					break;
				}

				circle_center = image_min + ImVec2{ cx, cy };
				circle_radius = project_radius( center_world, foundation::vec3{ 0.0f, 0.0f, 0.0f },
							hitbox.radius, cx, cy );
				break;
			}
		}

		if ( circle_radius > 0.0f )
		{
			draw->AddCircle( circle_center, circle_radius, packed( to_imvec( player.m_head_circle.color ) ), 40,
				std::max( 1.0f, player.m_head_circle.thickness ) );
		}
	}
	if ( player.m_view_line.enabled )
	{

		auto view_line_end = skeleton_points[ 7 ] + ImVec2{ -32.0f, -7.0f };
		if ( viewport_srv )
		{
			foundation::vec3 head{};
			float x{}, y{};

			foundation::vec3 forward{};
			if ( chams::g_preview.bone_position( 7, head )
				&& chams::g_preview.bone_direction( 7, foundation::vec3{ 1.0f, 0.0f, 0.0f }, forward )
				&& chams::g_preview.project( head + forward * std::max( 1.0f, player.m_view_line.length ), x, y ) )
			{
				view_line_end = image_min + ImVec2{ x, y };
			}
		}

		draw->AddLine( skeleton_points[ 7 ], view_line_end,
			packed( to_imvec( player.m_view_line.color ) ), std::max( 1.0f, player.m_view_line.thickness ) );
	}

	const auto anchor = [ & ]( const config::visual_profile::player::layout_element& element )
		{
			return ImVec2{
				config::visual_profile::player::resolve_layout_axis( unit_min.x, unit_max.x, element.x,
					config::visual_profile::player::layout_reference_width ),
				config::visual_profile::player::resolve_layout_axis( unit_min.y, unit_max.y, element.y,
					config::visual_profile::player::layout_reference_height ) };
		};
	const auto draw_preview_text = [ & ]( std::string_view text, ImFont* font, float size, ImVec2 center, ImU32 color )
		{
			const auto measured = font->CalcTextSizeA( size, FLT_MAX, 0.0f, text.data( ), text.data( ) + text.size( ) );
			const auto position = center - measured * 0.5f;
			const auto outline = IM_COL32( 0, 0, 0, 225 );
			draw->AddText( font, size, position + ImVec2{ -1, 0 }, outline, text.data( ), text.data( ) + text.size( ) );
			draw->AddText( font, size, position + ImVec2{ 1, 0 }, outline, text.data( ), text.data( ) + text.size( ) );
			draw->AddText( font, size, position + ImVec2{ 0, -1 }, outline, text.data( ), text.data( ) + text.size( ) );
			draw->AddText( font, size, position + ImVec2{ 0, 1 }, outline, text.data( ), text.data( ) + text.size( ) );
			draw->AddText( font, size, position, color, text.data( ), text.data( ) + text.size( ) );
			return std::pair{ position, position + measured };
		};
	const std::array dock_points{
		ImVec2{ unit_min.x - 12.0f, ( unit_min.y + unit_max.y ) * 0.5f },
		ImVec2{ ( unit_min.x + unit_max.x ) * 0.5f, unit_min.y - 12.0f },
		ImVec2{ ( unit_min.x + unit_max.x ) * 0.5f, unit_max.y + 12.0f },
		ImVec2{ unit_max.x + 12.0f, ( unit_min.y + unit_max.y ) * 0.5f }
	};
	const auto interact = [ & ]( int id, config::visual_profile::player::layout_element& element,
		ImVec2 item_min, ImVec2 item_max, bool allow_docking,
		int settings_rows, auto&& settings )
		{
			int dock_target = -1;
			const auto constrain = [ & ]( ImVec2 min, ImVec2 max )
				{
					ImVec2 correction{};
					if ( min.x < stage_min.x ) correction.x = stage_min.x - min.x;
					else if ( max.x > stage_max.x ) correction.x = stage_max.x - max.x;
					if ( min.y < stage_min.y ) correction.y = stage_min.y - min.y;
					else if ( max.y > stage_max.y ) correction.y = stage_max.y - max.y;
					element.x += correction.x / unit_size.x;
					element.y += correction.y / unit_size.y;
				};
			constrain( item_min, item_max );
			constexpr auto hit_padding = 5.0f;
			const auto hit_min = item_min - ImVec2{ hit_padding, hit_padding };
			const auto hit_max = item_max + ImVec2{ hit_padding, hit_padding };
			ImGui::PushID( id );
			ImGui::SetCursorScreenPos( hit_min );
			ImGui::InvisibleButton( "##preview_element", hit_max - hit_min );
			const auto hovered = ImGui::IsItemHovered( );
			if ( hovered ) ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeAll );
			if ( ImGui::IsItemActive( ) && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
			{
				auto delta = ImGui::GetIO( ).MouseDelta;
				delta.x = std::clamp( delta.x, stage_min.x - item_min.x, stage_max.x - item_max.x );
				delta.y = std::clamp( delta.y, stage_min.y - item_min.y, stage_max.y - item_max.y );
				element.x += delta.x / unit_size.x;
				element.y += delta.y / unit_size.y;
			}
			const auto dragging = ImGui::IsItemActive( )
				&& ImGui::IsMouseDragging( ImGuiMouseButton_Left );
			if ( allow_docking && ( dragging || ImGui::IsItemDeactivated( ) ) )
			{
				if ( dragging )
				{
					for ( const auto& point : dock_points )
					{
						draw->AddCircleFilled( point, 4.5f, IM_COL32( 26, 25, 34, 235 ), 18 );
						draw->AddCircle( point, 5.0f, IM_COL32( 255, 255, 255, 92 ), 18, 1.2f );
					}
				}
				const auto mouse = ImGui::GetMousePos( );
				auto nearest_distance = 28.0f;
				for ( int point = 0; point < static_cast<int>( dock_points.size( ) ); ++point )
				{
					const auto delta = mouse - dock_points[ point ];
					const auto distance = std::sqrt( delta.x * delta.x + delta.y * delta.y );
					if ( distance < nearest_distance )
					{
						nearest_distance = distance;
						dock_target = point;
					}
				}
				if ( dragging && dock_target >= 0 )
				{
					draw->AddCircleFilled( dock_points[ dock_target ], 8.0f,
						packed( ImVec4{ k_accent.x, k_accent.y, k_accent.z, 0.32f } ), 24 );
					draw->AddCircle( dock_points[ dock_target ], 7.0f,
						packed( k_accent ), 24, 2.0f );
				}
				if ( !ImGui::IsItemDeactivated( ) ) dock_target = -1;
			}
			if ( hovered && ImGui::GetIO( ).MouseWheel != 0.0f )
			{
				element.scale = std::clamp( element.scale + ImGui::GetIO( ).MouseWheel * 0.08f, 0.55f, 2.0f );
			}
			if ( hovered || ImGui::IsItemActive( ) )
			{
				draw->AddRect( hit_min, hit_max, IM_COL32( 255, 255, 255, 150 ), 5.0f, 0, 1.0f );
			}
			visual_editor_settings_popup( settings_rows, std::forward<decltype( settings )>( settings ) );
			ImGui::PopID( );
			return dock_target;
		};

	const auto* regular_wrapper = app::context().overlay.fonts( ).menu_regular_12;
	const auto* semibold_wrapper = app::context().overlay.fonts( ).menu_semibold_13;
	const auto* weapon_wrapper = app::context().overlay.fonts( ).weapons_15;
	auto* regular_font = regular_wrapper && regular_wrapper->im_font ? regular_wrapper->im_font : ImGui::GetFont( );
	auto* semibold_font = semibold_wrapper && semibold_wrapper->im_font ? semibold_wrapper->im_font : ImGui::GetFont( );
	auto* weapon_font = weapon_wrapper && weapon_wrapper->im_font ? weapon_wrapper->im_font : ImGui::GetFont( );
	const auto regular_size = regular_wrapper ? regular_wrapper->font_size : ImGui::GetFontSize( );
	const auto semibold_size = semibold_wrapper ? semibold_wrapper->font_size : ImGui::GetFontSize( );
	const auto weapon_size = weapon_wrapper ? weapon_wrapper->font_size : ImGui::GetFontSize( );

	if ( player.m_name.enabled )
	{
		const auto bounds = draw_preview_text( "PLAYER", semibold_font, semibold_size * player.m_layout.name.scale,
			anchor( player.m_layout.name ), packed( to_imvec( player.m_name.color ) ) );
		interact( 0, player.m_layout.name, bounds.first, bounds.second, false, 1, [ & ]
			{
				color_row( "Color", player.m_name.color );
			} );
	}
	if ( player.m_weapon.enabled )
	{
		const auto show_icon = player.m_weapon.display == config::visual_profile::player::weapon::display_type::icon ||
			player.m_weapon.display == config::visual_profile::player::weapon::display_type::text_and_icon;
		const auto show_text = player.m_weapon.display == config::visual_profile::player::weapon::display_type::text ||
			player.m_weapon.display == config::visual_profile::player::weapon::display_type::text_and_icon;
		const auto icon = features::visuals::player_t::weapon_glyph( "m4a1_silencer" );
		const auto center = anchor( player.m_layout.weapon );
		const auto icon_size = weapon_size * player.m_layout.weapon.scale;
		const auto text_size = regular_size * player.m_layout.weapon.scale;
		const auto icon_extent = show_icon ? weapon_font->CalcTextSizeA( icon_size, FLT_MAX, 0.0f, icon.data( ), icon.data( ) + icon.size( ) ) : ImVec2{};
		constexpr std::string_view weapon_name{ "M4A1-S" };
		const auto text_extent = show_text ? regular_font->CalcTextSizeA( text_size, FLT_MAX, 0.0f,
			weapon_name.data( ), weapon_name.data( ) + weapon_name.size( ) ) : ImVec2{};
		const auto gap = show_icon && show_text ? 2.0f * player.m_layout.weapon.scale : 0.0f;
		const auto total_height = icon_extent.y + text_extent.y + gap;
		auto current_y = center.y - total_height * 0.5f;
		auto item_min = ImVec2{ FLT_MAX, FLT_MAX };
		auto item_max = ImVec2{ -FLT_MAX, -FLT_MAX };
		if ( show_icon )
		{
			const auto icon_center = ImVec2{ center.x, current_y + icon_extent.y * 0.5f };
			auto bounds = draw_preview_text( icon, weapon_font, icon_size, icon_center,
				packed( to_imvec( player.m_weapon.ammo.enabled
					? player.m_weapon.ammo.empty_color : player.m_weapon.icon_color ) ) );
			if ( player.m_weapon.ammo.enabled )
			{
				constexpr auto preview_ammo_fraction = 20.0f / 30.0f;
				const auto split_x = bounds.first.x + ( bounds.second.x - bounds.first.x )
					* ( 1.0f - preview_ammo_fraction );
				draw->PushClipRect( { split_x, bounds.first.y - 2.0f },
					bounds.second + ImVec2{ 2.0f, 2.0f }, true );
				draw_preview_text( icon, weapon_font, icon_size, icon_center,
					packed( to_imvec( player.m_weapon.icon_color ) ) );
				draw->PopClipRect( );
			}
			item_min = { std::min( item_min.x, bounds.first.x ), std::min( item_min.y, bounds.first.y ) };
			item_max = { std::max( item_max.x, bounds.second.x ), std::max( item_max.y, bounds.second.y ) };
			current_y += icon_extent.y + gap;
		}
		if ( show_text )
		{
			const auto bounds = draw_preview_text( weapon_name, regular_font, text_size,
				{ center.x, current_y + text_extent.y * 0.5f }, packed( to_imvec( player.m_weapon.text_color ) ) );
			item_min = { std::min( item_min.x, bounds.first.x ), std::min( item_min.y, bounds.first.y ) };
			item_max = { std::max( item_max.x, bounds.second.x ), std::max( item_max.y, bounds.second.y ) };
		}
		if ( show_icon || show_text )
			interact( 1, player.m_layout.weapon, item_min, item_max, false,
				k_weapon_settings_rows, [ & ] { player_weapon_settings_rows( player.m_weapon ); } );
	}
	const auto draw_preview_bar = [ & ]( const auto& bar,
		const config::visual_profile::player::layout_element& layout, float fraction )
	{
		const auto center = anchor( layout );
		const auto scale = layout.scale;
		const auto position = static_cast<int>( bar.position );
		const auto vertical = position == 0 || position == 3;
		const auto thickness = std::clamp( bar.thickness, 1.0f, 12.0f ) * scale;
		const auto size = vertical ? ImVec2{ thickness, unit_size.y * scale }
			: ImVec2{ unit_size.x * scale, thickness };
		const auto bar_min = center - size * 0.5f;
		const auto bar_max = center + size * 0.5f;
		const auto outline = std::clamp( bar.outline_thickness, 0.5f, 4.0f ) * scale;
		if ( bar.outline )
			draw->AddRectFilled( bar_min - ImVec2{ outline, outline },
				bar_max + ImVec2{ outline, outline }, packed( to_imvec( bar.outline_color ) ) );
		draw->AddRectFilled( bar_min, bar_max, packed( to_imvec( bar.background_color ) ) );
		const auto clamped = std::clamp( fraction, 0.0f, 1.0f );
		const auto fill_min = vertical
			? ImVec2{ bar_min.x, bar_max.y - size.y * clamped }
			: bar_min;
		const auto fill_max = vertical
			? bar_max
			: ImVec2{ bar_min.x + size.x * clamped, bar_max.y };
		if ( bar.gradient )
		{
			if ( vertical )
				draw->AddRectFilledMultiColor( fill_min, fill_max,
					packed( to_imvec( bar.full_color ) ), packed( to_imvec( bar.full_color ) ),
					packed( to_imvec( bar.low_color ) ), packed( to_imvec( bar.low_color ) ) );
			else
				draw->AddRectFilledMultiColor( fill_min, fill_max,
					packed( to_imvec( bar.low_color ) ), packed( to_imvec( bar.full_color ) ),
					packed( to_imvec( bar.full_color ) ), packed( to_imvec( bar.low_color ) ) );
		}
		else draw->AddRectFilled( fill_min, fill_max, packed( to_imvec( bar.full_color ) ) );

		const auto segments = std::clamp( bar.segments, 1, 10 );
		const auto gap = std::clamp( bar.segment_gap, 0.0f, 4.0f ) * scale;
		const auto separator = packed( to_imvec( bar.outline ? bar.outline_color : bar.background_color ) );
		for ( int segment = 1; segment < segments && gap > 0.0f; ++segment )
		{
			const auto ratio = static_cast<float>( segment ) / segments;
			if ( vertical )
			{
				const auto y = bar_min.y + size.y * ratio;
				draw->AddRectFilled( { bar_min.x, y - gap * 0.5f }, { bar_max.x, y + gap * 0.5f }, separator );
			}
			else
			{
				const auto x = bar_min.x + size.x * ratio;
				draw->AddRectFilled( { x - gap * 0.5f, bar_min.y }, { x + gap * 0.5f, bar_max.y }, separator );
			}
		}
		return std::pair{ bar_min - ImVec2{ outline, outline },
			bar_max + ImVec2{ outline, outline } };
	};
	if ( player.m_health_bar.enabled )
	{
		const auto bounds = draw_preview_bar( player.m_health_bar, player.m_layout.health, 0.76f );
		const auto dock = interact( 2, player.m_layout.health, bounds.first, bounds.second, true,
			k_bar_settings_rows, [ & ] { player_bar_settings_rows( player.m_health_bar, player.m_layout.health ); } );
		if ( dock >= 0 )
		{
			dock_player_bar( player.m_layout.health, dock );
			player.m_health_bar.position =
				static_cast<config::visual_profile::player::health_bar::position_type>( dock );
		}
	}
	if ( player.m_armor_bar.enabled )
	{
		const auto bounds = draw_preview_bar( player.m_armor_bar, player.m_layout.armor, 0.84f );
		const auto dock = interact( 3, player.m_layout.armor, bounds.first, bounds.second, true,
			k_bar_settings_rows, [ & ] { player_bar_settings_rows( player.m_armor_bar, player.m_layout.armor ); } );
		if ( dock >= 0 )
		{
			dock_player_bar( player.m_layout.armor, dock );
			player.m_armor_bar.position =
				static_cast<config::visual_profile::player::armor_bar::position_type>( dock );
		}
	}
	{

		const auto& info = player.m_info_flags;
		using flag = config::visual_profile::player::info_flags::flag;

		struct preview_flag
		{
			std::string_view text;
			const config::visual_profile::player::info_flags::style* style{};
			flag kind{};
		};
		std::array<preview_flag, 9> flags{};
		std::size_t flag_count{};
		const auto add_flag = [ & ]( bool shown, std::string_view text,
			const config::visual_profile::player::info_flags::style& style, flag kind )
		{
			if ( shown && flag_count < flags.size( ) )
			{
				flags[ flag_count++ ] = { text, &style, kind };
			}
		};

		add_flag( info.enabled && info.has( flag::money ), "$4200", info.money_style, flag::money );
		add_flag( info.enabled && info.has( flag::armor ), "100 HK", info.armor_style, flag::armor );
		add_flag( info.enabled && info.has( flag::scoped ), "ZOOM", info.scoped_style, flag::scoped );
		add_flag( info.enabled && info.has( flag::defusing ), "DEFUSING", info.defusing_style, flag::defusing );
		add_flag( info.enabled && info.has( flag::ping ), "42MS", info.ping_style, flag::ping );
		add_flag( info.enabled && info.has( flag::distance ), "18M", info.distance_style, flag::distance );
		add_flag( info.enabled && info.has( flag::bomb_damage ), "-48 HP", info.bomb_damage_style, flag::bomb_damage );
		add_flag( info.enabled && info.has( flag::kit ), "KIT", info.kit_style, flag::kit );
		add_flag( info.enabled && info.has( flag::flashed ), "FLASHED", info.flashed_style, flag::flashed );

		if ( flag_count > 0 )
		{
			const auto scale = player.m_layout.flags.scale;
			const auto start = anchor( player.m_layout.flags );
			auto current_y = start.y;
			auto max_width = 0.0f;
			for ( std::size_t i = 0; i < flag_count; ++i )
			{
				const auto& [ text, style, kind ] = flags[ i ];
				const auto size = regular_size * 0.72f * scale * style->scale;
				const auto measured = regular_font->CalcTextSizeA( size, FLT_MAX, 0.0f, text.data( ), text.data( ) + text.size( ) );
				if ( kind == flag::kit )
				{
					const auto glyph_size = weapon_size * scale * style->scale;
					constexpr std::string_view glyph{ "r" };
					const auto extent = weapon_font->CalcTextSizeA( glyph_size, FLT_MAX, 0.0f,
						glyph.data( ), glyph.data( ) + glyph.size( ) );
					draw->AddText( weapon_font, glyph_size, { start.x, current_y },
						packed( to_imvec( style->color ) ), glyph.data( ), glyph.data( ) + glyph.size( ) );
					max_width = std::max( max_width, extent.x );
					current_y += extent.y;
				}
				else if ( kind == flag::flashed )
				{
					const auto glyph_size = weapon_size * scale * style->scale;
					constexpr std::string_view glyph{ "i" };
					const auto extent = weapon_font->CalcTextSizeA( glyph_size, FLT_MAX, 0.0f,
						glyph.data( ), glyph.data( ) + glyph.size( ) );
					draw->AddText( weapon_font, glyph_size, { start.x, current_y },
						packed( to_imvec( style->color ) ), glyph.data( ), glyph.data( ) + glyph.size( ) );
					max_width = std::max( max_width, extent.x );
					current_y += extent.y;
				}
				else
				{
					draw->AddText( regular_font, size, { start.x, current_y },
						packed( to_imvec( style->color ) ), text.data( ), text.data( ) + text.size( ) );
					max_width = std::max( max_width, measured.x );
					current_y += measured.y;
				}
			}
			interact( 5, player.m_layout.flags, start, { start.x + max_width, current_y }, false,
				k_info_flag_settings_rows, [ & ] { player_info_flag_settings_rows( player.m_info_flags ); } );
		}
	}

	draw->PopClipRect( );

	if ( viewport_srv )
	{
		const auto& io = ImGui::GetIO( );
		const auto inside = io.MousePos.x >= image_min.x && io.MousePos.x <= image_max.x
			&& io.MousePos.y >= image_min.y && io.MousePos.y <= image_max.y;

		if ( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		{
			g_preview_orbiting = false;
		}
		else if ( !g_preview_orbiting && ImGui::IsMouseClicked( ImGuiMouseButton_Left )
			&& inside && !ImGui::IsAnyItemActive( ) )
		{
			g_preview_orbiting = true;
		}

		if ( g_preview_orbiting )
		{
			chams::g_preview.orbit( io.MouseDelta.x );
			ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
		}

	}

	ImGui::End( );
	ImGui::PopStyleVar( 4 );
}

void menu_t::draw_combat( bool triggerbot )
{
	ImGui::SetCursorPos({0,0});
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
	ImGui::BeginChild( triggerbot ? "##trigger_cards" : "##aim_cards", { 0.0f, 0.0f }, false );
	begin_cards( triggerbot ? "##trigger_grid" : "##aim_grid" );

	auto& global = config::combat_settings.global;
	static constexpr const char* activation_modes[ ]{ "Hold", "Always On", "Toggle" };
	static constexpr const char* fov_modes[ ]{ "Fixed", "Distance", "Target" };
	const auto checks_row = [ & ]( config::combat_profile::legit_checks& checks )
	{
		int mask = ( checks.airborne ? 1 : 0 ) | ( checks.smoke ? 2 : 0 )
			| ( checks.flashed ? 4 : 0 );
		static constexpr std::pair<const char*, int> options[]{
			{ "Jump", 1 }, { "Smoke", 2 }, { "Flash", 4 } };
		multiselect_row( "Checks", mask, options, 7 );
		checks.airborne = ( mask & 1 ) != 0;
		checks.smoke = ( mask & 2 ) != 0;
		checks.flashed = ( mask & 4 ) != 0;
	};
	const auto visibility_row = [ & ]( config::combat_profile::legit_checks& checks )
	{
		auto only_visible = checks.walls == config::combat_profile::wall_policy::block;
		toggle_row( "Only Visible", only_visible );
		checks.walls = only_visible ? config::combat_profile::wall_policy::block
			: config::combat_profile::wall_policy::penetration;
	};
	const auto trigger_timing_row = [ & ]( int& delay, int& randomize,
		float& outlier_chance, int& outlier_delay, int& delay_after )
	{
		settings_popup_row( "Shot Timing", 5, [ & ]
			{
				slider_row( "Delay Before", delay, 0, 500, " ms" );
				slider_row( "Randomize", randomize, 0, 100, " ms" );
				slider_row( "Outlier Chance", outlier_chance, 0.0f, 25.0f, "%", 0.5f );
				slider_row( "Outlier Delay", outlier_delay, 0, 500, " ms" );
				slider_row( "Delay After", delay_after, 0, 1000, " ms" );
			} );
	};
	const auto prediction_row = [ & ]( config::combat_profile::prediction_settings& prediction )
	{
		toggle_popup_row( "Prediction", prediction.enabled, 2, [ & ]
			{
				slider_row( "Max Horizon", prediction.max_horizon_ms,
					0.0f, 120.0f, " ms", 1.0f );
				toggle_row( "Acceleration", prediction.acceleration );
			} );
	};
	const auto master_row = [ & ]( bool& enabled, int& mode )
	{
		toggle_popup_row( "Master Switch", enabled, 1, [ & ]
			{
				select_row( "Mode", mode, activation_modes );
			} );
	};
	const auto damage_override_row = [ & ]( bool& enabled, float& value,
		int& mode, int& key )
	{
		const auto rows = mode == config::combat_profile::activation::always ? 2 : 3;
		toggle_popup_row( "Damage Override", enabled, rows, [ & ]
			{
				select_row( "Mode", mode, activation_modes );
				if ( mode != config::combat_profile::activation::always )
					keybind_row( "Key", key );
				slider_row( "Override Damage", value, 1.0f, 100.0f, "", 1.0f );
			} );
	};
	const auto fov_row = [ & ]( int& fixed_fov,
		config::combat_profile::fov_settings& fov, bool& draw_area,
		zdraw::rgba& color )
	{
		const auto dynamic = fov.selection != config::combat_profile::fov_settings::fixed;
		const auto target_mode = fov.selection
			== config::combat_profile::fov_settings::target_distance;
		auto& near_distance = target_mode ? fov.target_near_distance_m : fov.near_distance_m;
		auto& near_fov = target_mode ? fov.target_near_fov : fov.near_fov;
		auto& far_distance = target_mode ? fov.target_far_distance_m : fov.far_distance_m;
		auto& far_fov = target_mode ? fov.target_far_fov : fov.far_fov;
		auto& curve = target_mode ? fov.target_distance_curve : fov.distance_curve;
		settings_popup_row( "FOV", dynamic ? 4 : 3, [ & ]
			{
				select_row( "Mode", fov.selection, fov_modes );
				if ( !dynamic )
				{
					slider_row( "Radius", fixed_fov, 1, 360, "°" );
				}
				else
				{
					slider_row( "Max Radius", near_fov, 2.0f, 45.0f, "°", 0.5f );
					settings_popup_row( "Advanced", 5, [ & ]
						{
							slider_row( "Full Size At", near_distance, 0.5f, 10.0f, " m", 0.5f );
							slider_row( "Max Radius", near_fov, 2.0f, 45.0f, "°", 0.5f );
							slider_row( "Min Size At", far_distance, 10.0f, 100.0f, " m", 0.5f );
							slider_row( "Min Radius", far_fov, 0.25f, 15.0f, "°", 0.25f );
							slider_row( "Falloff", curve, 0.25f, 4.0f, "", 0.05f );
							} );
				}
				toggle_color_row( "Visualization", draw_area, color );
			} );
	};
	const auto humanizer_row = [ & ]( int& amount, int& smoothing,
		config::combat_profile::humanizer_settings& humanizer )
	{
		settings_popup_row( "Humanizer Profile", 6, [ & ]
			{
				humanizer_preview( amount, smoothing, humanizer );
				slider_row( "Amount", amount, 0, 100, "%" );
				slider_row( "Smoothing", smoothing, 0, 50 );
				settings_popup_row( "Motion", 4, [ & ]
					{
						slider_row( "Gravity", humanizer.gravity, 0.0f, 20.0f, "", 0.05f );
						slider_row( "Wind", humanizer.wind, 0.0f, 20.0f, "", 0.05f );
						slider_row( "Max Step", humanizer.max_step, 1.0f, 90.0f, "°", 0.5f );
						slider_row( "Damping", humanizer.damping, 0.0f, 1.0f, "", 0.01f );
					} );
				settings_popup_row( "Behavior", 7, [ & ]
					{
						slider_row( "Reaction Min", humanizer.reaction_min_ms, 0, 500, " ms" );
						slider_row( "Reaction Max", humanizer.reaction_max_ms, 0, 750, " ms" );
						slider_row( "Curve", humanizer.curve, 0.0f, 1.0f, "", 0.01f );
						slider_row( "Overshoot Chance", humanizer.overshoot_chance, 0.0f, 100.0f, "%", 1.0f );
						slider_row( "Overshoot Amount", humanizer.overshoot_amount, 0.0f, 1.0f, "", 0.01f );
						slider_row( "Jitter", humanizer.jitter, 0.0f, 3.0f, "", 0.05f );
						slider_row( "Deadzone", humanizer.deadzone, 0.0f, 2.0f, "°", 0.05f );
					} );
			} );
	};
	const auto multipoint_row = [ & ]( bool& enabled,
		config::combat_profile::multipoint_settings& settings )
	{
		toggle_popup_row( "Multi-Point", enabled, 5, [ & ]
			{
				toggle_row( "Cap Points", settings.caps );
				toggle_row( "Side Points", settings.sides );
				slider_row( "Head Scale", settings.head_scale, 0.05f, 0.95f, "", 0.05f );
				slider_row( "Body Scale", settings.body_scale, 0.05f, 0.95f, "", 0.05f );
				slider_row( "Limbs Scale", settings.limb_scale, 0.05f, 0.95f, "", 0.05f );
			} );
	};
	const auto rcs_row = [ & ]( config::combat_profile::rcs_settings& rcs )
	{
		toggle_popup_row( "RCS", rcs.enabled, 7, [ & ]
			{
				slider_row( "Start Bullet", rcs.start_bullet, 1, 10 );
				slider_row( "Pitch Strength", rcs.pitch, 0.0f, 200.0f, "%", 1.0f );
				slider_row( "Yaw Strength", rcs.yaw, 0.0f, 200.0f, "%", 1.0f );
				slider_row( "Correction Time", rcs.response_ms, 1.0f, 150.0f, " ms", 1.0f );
				slider_row( "Smoothness", rcs.smoothness, 0.0f, 100.0f, "%", 1.0f );
				slider_row( "Strength Variation", rcs.randomness, 0.0f, 30.0f, "%", 1.0f );
				slider_row( "Path Drift", rcs.drift, 0.0f, 30.0f, "%", 1.0f );
			} );
	};
	if ( this->m_weapon_group < 0 )
	{
		if ( !triggerbot )
		{
			const auto aim_uses_key = global.aimbot_activation_mode
				!= config::combat_profile::activation::always;
			const auto targeting_rows = global.aimbot_enabled
				? ( aim_uses_key ? 9 : 8 ) : 1;
			card_in_column( "targeting", "TARGETING", targeting_rows, 0, [ & ]
				{
					master_row( global.aimbot_enabled, global.aimbot_activation_mode );
					if ( global.aimbot_enabled )
					{
						if ( aim_uses_key ) keybind_row( "Key", global.aimbot_key );
						checks_row( global.aimbot_checks );
						aim_parts_row( global.aimbot_hitbox_parts );
						fov_row( global.aimbot_fov, global.aimbot_fov_config,
							global.aimbot_draw_fov, global.aimbot_fov_color );
						humanizer_row( global.aimbot_humanize,
							global.aimbot_smoothing, global.aimbot_humanizer );
						multipoint_row( global.aimbot_multipoint,
							global.aimbot_multipoint_config );
						prediction_row( global.aimbot_prediction );
						toggle_row( "Lethal Only", global.aimbot_lethal_only );
					}
				} );
			card_in_column( "recoil", "RECOIL CONTROL", 1, 1, [ & ]
				{ rcs_row( global.aimbot_rcs ); } );
			if ( global.aimbot_enabled )
				card_in_column( "penetration", "PENETRATION", 3, 1, [ & ]
					{
						visibility_row( global.aimbot_checks );
						slider_row( "Min Damage", global.aimbot_min_damage, 1.0f, 100.0f, "", 1.0f );
						damage_override_row( global.aimbot_min_damage_override_enabled,
							global.aimbot_min_damage_override,
							global.aimbot_min_damage_override_mode,
							global.aimbot_min_damage_override_key );
					} );
		}
		else
		{
			static constexpr const char* seed_types[ ]{ "None", "Restricted", "Unrestricted" };
			const auto global_seeded = global.triggerbot_seed_type != config::combat_profile::seed_mode::none;
			const auto trigger_uses_key = global.triggerbot_activation_mode
				!= config::combat_profile::activation::always;
			const auto trigger_rows = global.triggerbot_enabled
				? ( trigger_uses_key ? 1 : 0 ) + ( global_seeded ? 7 : 8 ) : 1;
			card_in_column( "trigger_core", "TRIGGERBOT CORE", trigger_rows, 0, [ & ]
				{
					master_row( global.triggerbot_enabled, global.triggerbot_activation_mode );
					if ( global.triggerbot_enabled )
					{
						if ( trigger_uses_key ) keybind_row( "Key", global.triggerbot_key );
						checks_row( global.triggerbot_checks );
						select_row( "Seed Type", global.triggerbot_seed_type, seed_types );
						aim_parts_row( global.triggerbot_hitbox_parts );
						if ( global_seeded )
							slider_row( "Reaction Time (ms)", global.triggerbot_reaction_time, 0, 400 );
						else
						{
							slider_row( "Hitchance", global.triggerbot_hitchance, 0.0f, 100.0f, "%", 1.0f );
							trigger_timing_row( global.triggerbot_delay,
								global.triggerbot_randomize_ms, global.triggerbot_outlier_chance,
								global.triggerbot_outlier_delay_ms, global.triggerbot_delay_after_ms );
						}
						toggle_row( "Predictive", global.triggerbot_predictive );
						toggle_row( "Lethal Only", global.triggerbot_lethal_only );
					}
				} );
			if ( global.triggerbot_enabled )
				card_in_column( "trigger_penetration", "PENETRATION", 3, 1, [ & ]
					{
						visibility_row( global.triggerbot_checks );
						slider_row( "Min Damage", global.triggerbot_min_damage, 1.0f, 100.0f, "", 1.0f );
						damage_override_row( global.triggerbot_min_damage_override_enabled,
							global.triggerbot_min_damage_override,
							global.triggerbot_min_damage_override_mode,
							global.triggerbot_min_damage_override_key );
					} );
		}
	}
	else
	{
		auto& group = config::combat_settings.overrides[ this->m_weapon_group ];
		card( "override", "OVERRIDE SETTINGS", 1, [ & ] { toggle_row( "Inherit Global Settings", group.use_global ); } );
		if ( !group.use_global && !triggerbot && global.aimbot_enabled )
		{
			card_in_column( "targeting", "TARGETING", 7, 0, [ & ]
				{
					checks_row( group.aimbot_checks );
					aim_parts_row( group.aimbot_hitbox_parts );
					fov_row( group.aimbot_fov, group.aimbot_fov_config,
						global.aimbot_draw_fov, global.aimbot_fov_color );
					humanizer_row( group.aimbot_humanize,
						group.aimbot_smoothing, group.aimbot_humanizer );
					multipoint_row( group.aimbot_multipoint,
						group.aimbot_multipoint_config );
					prediction_row( group.aimbot_prediction );
					toggle_row( "Lethal Only", group.aimbot_lethal_only );
				} );
			card_in_column( "recoil", "RECOIL CONTROL", 1, 1, [ & ]
				{ rcs_row( group.aimbot_rcs ); } );
			card( "penetration", "PENETRATION", 2, [ & ]
				{
					visibility_row( group.aimbot_checks );
					slider_row( "Min Damage", group.aimbot_min_damage, 1.0f, 100.0f, "", 1.0f );
				} );
		}
		else if ( !group.use_global && triggerbot && global.triggerbot_enabled )
		{
			static constexpr const char* seed_types[ ]{ "None", "Restricted", "Unrestricted" };
			const auto group_seeded = group.triggerbot_seed_type != config::combat_profile::seed_mode::none;
			card_in_column( "trigger_core", "TRIGGERBOT CORE", group_seeded ? 6 : 7, 0, [ & ]
				{
					checks_row( group.triggerbot_checks );
					select_row( "Seed Type", group.triggerbot_seed_type, seed_types );
					aim_parts_row( group.triggerbot_hitbox_parts );
					if ( group_seeded )
					{
						slider_row( "Reaction Time (ms)", group.triggerbot_reaction_time, 0, 400 );
					}
					else
					{
						slider_row( "Hitchance", group.triggerbot_hitchance, 0.0f, 100.0f, "%", 1.0f );
						trigger_timing_row( group.triggerbot_delay,
							group.triggerbot_randomize_ms, group.triggerbot_outlier_chance,
							group.triggerbot_outlier_delay_ms, group.triggerbot_delay_after_ms );
					}
					toggle_row( "Predictive", group.triggerbot_predictive );
					toggle_row( "Lethal Only", group.triggerbot_lethal_only );
				} );
			card_in_column( "trigger_penetration", "PENETRATION", 2, 1, [ & ]
				{
					visibility_row( group.triggerbot_checks );
					slider_row( "Min Damage", group.triggerbot_min_damage, 1.0f, 100.0f, "", 1.0f );
				} );
		}
	}

	end_cards( );
	ImGui::EndChild( );
	ImGui::PopStyleVar( );
}

void menu_t::draw_visuals( )
{
	ImGui::SetCursorPos({0,0});
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
	ImGui::BeginChild( "##visual_cards", { 0.0f, 0.0f }, false );
	begin_cards( "##visual_grid" );

	if ( this->m_visual_group == 0 )
	{
		auto& p = config::visual_settings.m_player;
		card( "master", "MASTER", 2, [ & ]
			{
				const auto activation_rows = p.activation_mode
					== config::visual_profile::player::always_on ? 2 : 3;
				toggle_popup_row( "Enable Player ESP", p.enabled, activation_rows, [ & ]
					{
						static constexpr const char* modes[]{ "Always On", "Hold", "Toggle" };
						select_row( "Mode", p.activation_mode, modes );
						if ( p.activation_mode != config::visual_profile::player::always_on )
							keybind_row( "Key", p.activation_key );
						toggle_row( "Spectator Sync", p.spectator_sync );
					} );
				toggle_popup_row( "Legit Sync", p.m_legit_sync.enabled, 9, [ & ]
					{
						toggle_row( "Direct Visibility", p.m_legit_sync.direct_visible );
						toggle_row( "Radar Spotted", p.m_legit_sync.radar );
						toggle_row( "Audible Sounds", p.m_legit_sync.sound );
						slider_row( "Radar Hold", p.m_legit_sync.radar_hold, 0.1f, 10.0f, "s", 0.1f );
						slider_row( "Sound Hold", p.m_legit_sync.sound_hold, 0.1f, 10.0f, "s", 0.1f );
						slider_row( "Hearing Distance", p.m_legit_sync.sound_distance, 100.0f, 2500.0f, "u", 10.0f );
						auto minimum_opacity = p.m_legit_sync.pulse_min_opacity * 100.0f;
						auto maximum_opacity = p.m_legit_sync.pulse_max_opacity * 100.0f;
						slider_row( "Pulse Minimum", minimum_opacity, 0.0f, 100.0f, "%", 1.0f );
						slider_row( "Pulse Maximum", maximum_opacity,
							minimum_opacity, 100.0f, "%", 1.0f );
						p.m_legit_sync.pulse_min_opacity = minimum_opacity / 100.0f;
						p.m_legit_sync.pulse_max_opacity = maximum_opacity / 100.0f;
						slider_row( "Pulse Period", p.m_legit_sync.pulse_period,
							0.4f, 5.0f, "s", 0.1f );
					} );
			} );

		card( "box", "BOUNDING BOX", p.m_box.style == config::visual_profile::player::box::style_type::cornered ? 4 : 3, [ & ]
			{
				toggle_row( "Show Box", p.m_box.enabled );
				int style = static_cast<int>( p.m_box.style );
				static constexpr const char* styles[ ]{ "Full", "Cornered" };
				select_row( "Style", style, styles );
				p.m_box.style = static_cast<config::visual_profile::player::box::style_type>( style );
				if ( style == 1 ) slider_row( "Corner Length", p.m_box.corner_length, 4.0f, 30.0f, "", 0.5f );
				toggle_popup_row( "Background Fill", p.m_box.fill, 4, [ & ]
					{
						color_row( "Box Visible Color", p.m_box.visible_color );
						color_row( "Box Occluded Color", p.m_box.occluded_color );
						color_row( "Fill Visible Color", p.m_box.fill_visible_color );
						color_row( "Fill Occluded Color", p.m_box.fill_occluded_color );
					} );
			} );
		card( "details", "DETAILS", 8, [ & ]
			{
				toggle_color_row( "Show Name", p.m_name.enabled, p.m_name.color );
				toggle_popup_row( "Show Weapon", p.m_weapon.enabled, k_weapon_settings_rows, [ & ]
					{
						player_weapon_settings_rows( p.m_weapon );
					} );
				toggle_popup_row( "Health Bar", p.m_health_bar.enabled, k_bar_settings_rows, [ & ]
					{
						player_bar_settings_rows( p.m_health_bar, p.m_layout.health );
					} );
				toggle_popup_row( "Armor Bar", p.m_armor_bar.enabled, k_bar_settings_rows, [ & ]
					{
						player_bar_settings_rows( p.m_armor_bar, p.m_layout.armor );
					} );
				toggle_popup_row( "Info Flags", p.m_info_flags.enabled, k_info_flag_settings_rows, [ & ]
					{
						player_info_flag_settings_rows( p.m_info_flags );
					} );
				auto& chams_cfg = config::visual_settings.m_chams;

				toggle_popup_row( "Chams", chams_cfg.enabled, 5, [ & ]
					{
						toggle_row( "Antialiasing", chams_cfg.antialiasing );
						toggle_popup_row( "Model Glow", chams_cfg.glow_effect.enabled, 3, [ & ]
							{
								color_row( "Glow Color", chams_cfg.glow_effect.color );
								slider_row( "Glow Radius", chams_cfg.glow_effect.radius, 0.5f, 16.0f, "u", 0.5f );
								slider_row( "Glow Strength", chams_cfg.glow_effect.strength, 0.0f, 1.0f, "", 0.01f );
							} );
						toggle_popup_row( "Death Shatter", chams_cfg.kill_effect.enabled, 4, [ & ]
							{
								color_row( "Particle Color", chams_cfg.kill_effect.color );
								slider_row( "Particle Duration", chams_cfg.kill_effect.duration, 0.2f, 3.0f, "s", 0.05f );
								slider_row( "Particle Size", chams_cfg.kill_effect.size, 1.0f, 8.0f, "px", 0.5f );
								slider_row( "Particle Count", chams_cfg.kill_effect.count, 4, 32 );
							} );
						toggle_popup_row( "On Hit Chams", chams_cfg.on_shot.enabled, 2, [ & ]
							{
								slider_row( "Ghost Duration", chams_cfg.on_shot.duration, 0.1f, 3.0f, "s", 0.05f );
								settings_popup_row( "Ghost Appearance",
									chams_material_row_count( chams_cfg.on_shot.appearance ), [ & ]
									{
										chams_material_rows( chams_cfg.on_shot.appearance );
									} );
							} );
						settings_popup_row( "Occlusion", 2, [ & ]
							{
								toggle_row( "Dynamic Doors", chams_cfg.occlude_dynamic_doors );
								toggle_row( "Smoke Occlusion", chams_cfg.occlude_smoke );
							} );
					} );
				toggle_popup_row( "Chams Visible", chams_cfg.visible.enabled,
					chams_material_row_count( chams_cfg.visible ), [ & ]
					{
						chams_material_rows( chams_cfg.visible );
					} );
				toggle_popup_row( "Chams Occluded", chams_cfg.invisible.enabled,
					chams_material_row_count( chams_cfg.invisible ), [ & ]
					{
						chams_material_rows( chams_cfg.invisible );
					} );
			} );
		card( "skeleton", "SKELETON & HITBOXES", 6, [ & ]
			{
				toggle_color_row( "Show Skeleton", p.m_skeleton.enabled, p.m_skeleton.visible_color );
				toggle_color_row( "Show Head Circle", p.m_head_circle.enabled, p.m_head_circle.color );
				toggle_color_row( "Show View Line", p.m_view_line.enabled, p.m_view_line.color );
				toggle_popup_row( "Offscreen Arrows", p.m_offscreen_arrows.enabled, 4, [ & ]
					{
						color_row( "Arrow Color", p.m_offscreen_arrows.color );
						slider_row( "Arrow Size", p.m_offscreen_arrows.size, 6.0f, 40.0f, "", 0.5f );
						slider_row( "Arrow Radius", p.m_offscreen_arrows.radius, 40.0f, 500.0f, "", 1.0f );
						toggle_popup_row( "Bloom", p.m_offscreen_arrows.bloom, 5, [ & ]
							{
								color_row( "Bloom Color", p.m_offscreen_arrows.bloom_color );
								slider_row( "Bloom Radius", p.m_offscreen_arrows.bloom_radius,
									1.0f, 16.0f, "px", 0.5f );
								slider_row( "Bloom Speed", p.m_offscreen_arrows.bloom_speed,
									0.05f, 2.0f, "Hz", 0.05f );
								slider_row( "Minimum Glow", p.m_offscreen_arrows.bloom_min_alpha,
									0.0f, 1.0f, "", 0.01f );
								slider_row( "Maximum Glow", p.m_offscreen_arrows.bloom_max_alpha,
									0.0f, 1.0f, "", 0.01f );
							} );
					} );
				toggle_popup_row( "Threat Hitboxes", p.m_threat_module.enabled, 3, [ & ]
					{
						color_row( "Head Hitbox Color", p.m_threat_module.head_color );
						color_row( "Body Hitbox Color", p.m_threat_module.body_color );
						slider_row( "Fill Alpha", p.m_threat_module.fill_alpha, 0.0f, 255.0f, "", 1.0f );
					} );

				auto& s = config::visual_settings.m_sound;
				toggle_popup_row( "Sound ESP", s.enabled, 4, [ & ]
					{
						color_row( "Ring Color", s.color );
						toggle_row( "Local Sync", s.local_sync );
						slider_row( "Duration", s.duration, 0.5f, 4.0f, " s", 0.05f );
						slider_row( "Ring Radius", s.radius, 10.0f, 80.0f, "", 0.5f );
					} );
			} );
	}
	else if ( this->m_visual_group == 1 )
	{
		auto& p = config::visual_settings.m_item;
		card( "item_settings", "SETTINGS", 4, [ & ]
			{
				toggle_row( "Enable Item ESP", p.enabled );
				slider_row( "Max Distance", p.max_distance, 5.0f, 150.0f, "", 1.0f );
				toggle_color_row( "Show Icon", p.m_icon.enabled, p.m_icon.color );

				toggle_row( "Show Item Name", p.m_name.enabled );
			} );
		card( "item_filters", "FILTERS", 6, [ & ]
			{
				toggle_row( "Rifles", p.m_filters.rifles );
				toggle_row( "SMGs", p.m_filters.smgs );
				toggle_row( "Snipers", p.m_filters.snipers );
				toggle_row( "Pistols", p.m_filters.pistols );
				toggle_row( "Grenades", p.m_filters.grenades );
				toggle_row( "Utility", p.m_filters.utility );
			} );
	}
	else if ( this->m_visual_group == 2 )
	{
		auto& p = config::visual_settings.m_projectile;
		card( "projectile_elements", "VISUAL ELEMENTS", 5, [ & ]
			{
				toggle_row( "Enable Projectiles", p.enabled );
				static constexpr const char* display_modes[ ]{ "Indicator", "Text Only" };
				select_row( "Display Mode", p.display_mode, display_modes );
				toggle_row( p.display_mode == config::visual_profile::projectile::text_only
					? "Show Text" : "Show Icon", p.show_icon );
				toggle_row( p.display_mode == config::visual_profile::projectile::text_only
					? "Effect Timer" : "Effect Timer Ring", p.show_timer_ring );
				toggle_row( "Inferno Bounds", p.show_inferno_bounds );
			} );
		card( "projectile_colors", "COLORS", 5, [ & ]
			{
				color_row( "HE Grenade", p.color_he );
				color_row( "Flashbang", p.color_flash );
				color_row( "Smoke", p.color_smoke );
				color_row( "Molotov", p.color_molotov );
				color_row( "Decoy", p.color_decoy );
			} );
		card( "projectile_indicator", "INDICATOR", 3, [ & ]
			{
				color_row( "Timer Full", p.timer_high_color );
				color_row( "Timer Low", p.timer_low_color );
				color_row( "Indicator Background", p.indicator_background );
			} );
		card( "projectile_inferno", "INFERNO GRADIENT", 2, [ & ]
			{
				slider_row( "Gradient Width", p.inferno_gradient_width, 8.0f, 80.0f, " px", 1.0f );
				slider_row( "Gradient Opacity", p.inferno_gradient_opacity, 0.0f, 100.0f, "%", 1.0f );
			} );
	}
	else if ( this->m_visual_group == 3 )
	{
		auto& p = config::visual_settings.m_bomb;
		card( "bomb_states", "STATES", 6, [ & ]
			{
				toggle_row( "Enable Bomb ESP", p.enabled );
				static constexpr const char* display_modes[ ]{ "Indicator", "Text Only" };
				select_row( "Display Mode", p.display_mode, display_modes );
				toggle_color_row( "Show Carrier", p.show_active_bomb, p.active_bomb_color );
				toggle_popup_row( "Show Planted", p.show_planted_bomb, 2, [ & ]
					{
						color_row( "Planted Color", p.bomb_color_t );
						color_row( "Defusing Color", p.bomb_color_ct );
					} );
				toggle_row( p.display_mode == config::visual_profile::bomb::text_only
					? "World Timer" : "World Timer Ring", p.show_timer );
				toggle_popup_row( "Bomb Info Panel", p.show_info_panel, 2, [ & ]
					{
						color_row( "Timer Color", p.timer_text_color );
						color_row( "Panel Background", p.panel_background );
					} );
			} );
		card( "bomb_safe_zone", "SAFE ZONE (BAKED)", 1, [ & ]
			{
				toggle_popup_row( "Safe Zone Contour", p.show_safe_zone, 4, [ & ]
					{
						color_row( "Zone Color", p.safe_zone_color );
						slider_row( "Gradient Bands", p.safe_zone_bands, 1, 8 );
						slider_row( "Band Step", p.safe_zone_band_step, 4.0f, 40.0f, "HP", 1.0f );
						slider_row( "Render Radius", p.safe_zone_draw_radius, 200.0f, 2500.0f, "u", 10.0f );
					} );
			} );
	}
	else if ( this->m_visual_group == 4 )
	{
		auto& p = config::visual_settings.m_radar;
		card_in_column( "radar_players", "RADAR PLAYERS", 5, 0, [ & ]
			{
				const auto uses_key = p.activation_mode
					!= config::visual_profile::radar::always_on;
				toggle_popup_row( "Overlay Enemy Markers", p.enabled,
					uses_key ? 2 : 1, [ & ]
					{
						int mode = p.activation_mode;
						static constexpr const char* modes[ ]{ "Always On", "Hold", "Toggle" };
						select_row( "Activation", mode, modes );
						p.activation_mode = std::clamp( mode,
							static_cast<int>( config::visual_profile::radar::always_on ),
							static_cast<int>( config::visual_profile::radar::toggle ) );
						if ( p.activation_mode != config::visual_profile::radar::always_on )
							keybind_row( "Key", p.activation_key );
					} );
				toggle_color_row( "Player Names", p.show_names, p.name_color );
				toggle_color_row( "Player Health", p.show_health, p.health_color );
				toggle_color_row( "Player Armor", p.show_armor, p.armor_color );
				toggle_color_row( "Player Weapon", p.show_weapon, p.weapon_color );
			} );
		card_in_column( "radar_style", "PLAYER STYLE", 5, 0, [ & ]
			{
				color_row( "Enemy Marker", p.enemy_color );
				color_row( "View Direction", p.direction_color );
				color_row( "Status Text", p.status_color );
				slider_row( "Marker Scale", p.marker_scale, 0.5f, 2.0f, "", 0.05f );
				toggle_popup_row( "Text Outline", p.text_outline, 2, [ & ]
					{
						color_row( "Outline Color", p.text_outline_color );
						slider_row( "Outline Thickness", p.text_outline_thickness,
							0.5f, 3.0f, " px", 0.5f );
					} );
			} );
		card_in_column( "radar_grenades", "RADAR GRENADES", 3, 1, [ & ]
			{
				toggle_popup_row( "Grenade Markers", p.show_projectiles, 6, [ & ]
					{
						color_row( "HE Color", p.he_color );
						color_row( "Flash Color", p.flash_color );
						color_row( "Smoke Color", p.smoke_color );
						color_row( "Molotov Color", p.molotov_color );
						color_row( "Decoy Color", p.decoy_color );
						slider_row( "Information Scale", p.information_scale,
							0.5f, 1.5f, "", 0.05f );
					} );
				toggle_popup_row( "Grenade Trajectories", p.show_trajectories, 2, [ & ]
					{
						slider_row( "Line Thickness", p.trajectory_thickness,
							0.5f, 6.0f, " px", 0.25f );
						slider_row( "End Point Size", p.trajectory_endpoint_size,
							1.0f, 10.0f, " px", 0.5f );
					} );
				toggle_popup_row( "Grenade Zones", p.show_grenade_zones, 3, [ & ]
					{
						slider_row( "Fill Opacity", p.zone_fill_alpha, 0.0f, 100.0f, "%", 1.0f );
						slider_row( "Outline Opacity", p.zone_outline_alpha, 0.0f, 100.0f, "%", 1.0f );
						slider_row( "Outline Thickness", p.zone_outline_thickness,
							0.5f, 5.0f, " px", 0.25f );
					} );
			} );
	}
	else if ( this->m_visual_group == 5 )
	{
		auto& general = config::general_settings;
		card( "bullet_effects", "BULLET EFFECTS", 3, [ & ]
			{
				auto& tracers = general.m_bullet_tracers;
				toggle_popup_row( "Enable Bullet Tracers", tracers.enabled, 5, [ & ]
					{
						settings_popup_row( "Appearance", 3, [ & ]
							{
								color_row( "Tracer Color", tracers.color );
								slider_row( "Thickness", tracers.thickness, 1.0f, 10.0f, "", 0.25f );
								toggle_row( "Bloom Effect", tracers.bloom );
							} );
						settings_popup_row( "Lifetime", 2, [ & ]
							{
								slider_row( "Fade Duration", tracers.duration, 1.0f, 10.0f, "s", 0.1f );
								slider_row( "Max Tracers", tracers.max_count, 1, 100 );
							} );
						toggle_row( "Draw Trajectory Line", tracers.draw_line );
						toggle_popup_row( "Impact Cubes", tracers.draw_cubes, 3, [ & ]
							{
								slider_row( "Cube Size", tracers.cube_half, 0.5f, 5.0f, "", 0.25f );
								color_row( "Cube Edge Color", tracers.cube_edge_color );
								slider_row( "Cube Face Alpha", tracers.cube_face_alpha, 0.0f, 160.0f, "", 1.0f );
							} );
						settings_popup_row( "Distance Fade", 2, [ & ]
							{
								slider_row( "Fade Near", tracers.fade_near, 5.0f, 200.0f, "u", 1.0f );
								slider_row( "Fade Far", tracers.fade_far, 50.0f, 500.0f, "u", 1.0f );
							} );
					} );

				auto& marker = general.m_hitmarker;
				toggle_popup_row( "World Hitmarker", marker.enabled, 3, [ & ]
					{
						color_row( "Marker Color", marker.color );
						settings_popup_row( "Geometry", 3, [ & ]
							{
								slider_row( "Marker Size", marker.size, 3.0f, 18.0f, "", 0.5f );
								slider_row( "Center Gap", marker.gap, 0.0f, 10.0f, "", 0.5f );
								slider_row( "Marker Thickness", marker.thickness, 1.0f, 4.0f, "", 0.25f );
							} );
						slider_row( "Marker Duration", marker.duration, 0.15f, 1.5f, "s", 0.05f );
					} );

				static constexpr std::array<const char*, 5> hitsound_styles{
					"Soft", "Glass", "Pluck", "Crisp", "Flesh" };
				auto& sound = general.m_hitsound;
				toggle_popup_row( "Hit Sound", sound.enabled, 4, [ & ]
					{
						select_row( "Sound", sound.style, hitsound_styles );
						slider_percent_row( "Volume", sound.volume );
						if ( button_row( "Preview", "Play" ) )
							features::visuals::hitsounds().play( sound.style, sound.volume );
						toggle_popup_row( "Floating Damage", sound.show_damage, 4, [ & ]
							{
								color_row( "Damage Color", sound.damage_color );
								slider_row( "Damage Size", sound.damage_size, 8.0f, 28.0f, "", 0.5f );
								slider_row( "Damage Duration", sound.damage_duration, 0.15f, 2.0f, "s", 0.05f );
								slider_row( "Damage Rise", sound.damage_rise, 0.0f, 100.0f, "px", 1.0f );
							} );
					} );
			} );

		auto& no_flash = config::visual_settings.m_no_flash;
		card( "no_flash", "NO FLASH (WIREFRAME)", 1, [ & ]
			{
				toggle_popup_row( "Enable Visual No Flash", no_flash.enabled, 3, [ & ]
					{
						slider_row( "Render Distance", no_flash.max_distance, 200.0f, 3000.0f, "", 10.0f );
						color_row( "Dimming Overlay Color", no_flash.background_color );
						color_row( "Wireframe Color", no_flash.wireframe_color );
					} );
			} );

		auto& no_smoke = config::visual_settings.m_no_smoke;
		card( "no_smoke", "NO SMOKE (WIREFRAME)", 1, [ & ]
			{
				toggle_popup_row( "Enable Visual No Smoke", no_smoke.enabled, 1, [ & ]
					{
						color_row( "Smoke Wireframe Color", no_smoke.wireframe_color );
					} );
			} );
	}
	else
	{
		auto& p = config::visual_settings.m_crosshair;
		card( "crosshair_settings", "CROSSHAIR SETTINGS", 5, [ & ]
			{
				toggle_row( "Enable Crosshair", p.enabled );
				toggle_row( "Copy Game Crosshair", p.copy_game );
				toggle_row( "Draw Dot", p.dot );
				toggle_popup_row( "Draw Lines", p.lines, 3, [ & ]
					{
						toggle_row( "T-Style", p.t_style );
						slider_row( "Length", p.length, 1.0f, 50.0f, "", 0.5f );
						slider_row( "Gap", p.gap, 0.0f, 50.0f, "", 0.5f );
					} );
				slider_row( "Thickness", p.thickness, 1.0f, 10.0f, "", 0.25f );
			} );
		card( "crosshair_colors", "COLORS", 3, [ & ]
			{
				toggle_popup_row( "Draw Outline", p.outline, 2, [ & ]
					{
						color_row( "Outline Color", p.outline_color );
						slider_row( "Outline Thickness", p.outline_thickness,
							0.5f, 3.0f, "", 0.5f );
					} );
				color_row( "Primary Color", p.color );
				toggle_popup_row( "Penetration Indicator", p.penetration_enabled, 3, [ & ]
					{
						color_row( "Can Penetrate", p.penetration_color_yes );
						color_row( "Cannot Penetrate", p.penetration_color_no );
						slider_row( "Min Damage", p.penetration_min_damage, 1.0f, 200.0f, "", 1.0f );
					} );
			} );
	}

	end_cards( );
	ImGui::EndChild( );
	ImGui::PopStyleVar( );
}

void menu_t::draw_misc( )
{
	ImGui::SetCursorPos({0,0});
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f } );
	ImGui::BeginChild( "##misc_cards", { 0.0f, 0.0f }, false );
	begin_cards( "##misc_grid" );
	auto& p = config::general_settings;

	if ( this->m_misc_group == 0 )
	{
		card_in_column( "interface", "INTERFACE", 3, 0, [ & ]
		{
			static constexpr const char* languages[]{ "English", "Русский" };
			const int previous = p.language;
			select_row( "Language", p.language, languages );
			if ( p.language != previous )
			{
				p.language = std::clamp( p.language, 0,
					static_cast<int>( render::localization::id::count ) - 1 );
				render::localization::set( static_cast<render::localization::id>( p.language ) );
			}
			static constexpr std::array dpi_scales{ 0.50f, 0.75f, 1.00f, 1.25f, 1.50f };
			static constexpr const char* dpi_labels[]{ "50%", "75%", "100%", "125%", "150%" };
			const auto closest_scale = std::min_element( dpi_scales.begin(), dpi_scales.end(),
				[ & ]( const float left, const float right )
				{ return std::abs( left - p.menu_scale ) < std::abs( right - p.menu_scale ); } );
			int dpi_index = static_cast<int>( std::distance( dpi_scales.begin(), closest_scale ) );
			select_row( "DPI Scale", dpi_index, dpi_labels );
			p.menu_scale = dpi_scales[ std::clamp( dpi_index, 0,
				static_cast<int>( dpi_scales.size() ) - 1 ) ];
			settings_popup_row( "Interface Colors", 3, [ & ]
			{
				settings_popup_row( "Typography", 2, [ & ]
				{
					color_row( "Primary Text", p.palette.text );
					color_row( "Muted Text", p.palette.muted_text );
				} );
				settings_popup_row( "Surfaces", 4, [ & ]
				{
					color_row( "Menu Background", p.palette.background );
					color_row( "Controls", p.palette.panel );
					color_row( "Containers", p.palette.card );
					color_row( "Popups", p.palette.popup );
				} );
				settings_popup_row( "Interaction", 3, [ & ]
				{
					color_row( "Accent", p.palette.accent );
					color_row( "Hover", p.palette.hover );
					color_row( "Borders", p.palette.border );
				} );
			} );
		} );
		card_in_column( "automation", "AUTOMATION", 1, 1, [ & ]
			{ toggle_row( "Auto Accept Match", p.auto_accept ); } );
		card_in_column( "engine", "ENGINE", 1, 1, [ & ]
		{
			toggle_popup_row( "FPS Limiter", p.limit_fps, 1, [ & ]
			{
				slider_row( "Maximum FPS", p.fps_limit, 30, 1000 );
			} );
		} );
	}
	else if ( this->m_misc_group == 1 )
	{
		auto& n = p.m_nade_helper;
		auto& assist = config::combat_settings.global.grenade_aim;
		card_in_column( "lineups", "LINEUP HELPER", 2, 0, [ & ]
		{
			toggle_popup_row( "Lineup Helper", n.enabled, 4, [ & ]
			{
				settings_popup_row( "Geometry", 5, [ & ]
				{
					slider_row( "Draw Distance", n.draw_distance, 200.0f, 4000.0f, "u", 10.0f );
					slider_row( "Marker Distance", n.stand_distance, 60.0f, 800.0f, "u", 5.0f );
					slider_row( "Stand Radius", n.stand_radius, 6.0f, 64.0f, "u", 1.0f );
					slider_row( "Release Radius", n.release_radius, 1.0f, 16.0f, "u", 0.5f );
					slider_row( "Height Tolerance", n.height_tolerance, 1.0f, 24.0f, "u", 0.5f );
				} );
				settings_popup_row( "Display", 2, [ & ]
				{
					toggle_row( "Show Throw Type", n.show_action );
					toggle_row( "Show Distance", n.show_distance );
				} );
				settings_popup_row( "Plaque Style", 3, [ & ]
				{
					color_row( "Background", n.plaque_background );
					color_row( "Text", n.plaque_text );
					color_row( "Accent", n.plaque_accent );
				} );
				settings_popup_row( "Marker Style", 3, [ & ]
				{
					color_row( "Stand Marker", n.stand_marker );
					color_row( "Stand Marker Active", n.stand_marker_active );
					color_row( "Aim Marker", n.aim_marker );
				} );
			} );
			toggle_popup_row( "Lineup Aim Assist", n.aim_assist, 3, [ & ]
			{
				keybind_row( "Aim Key", n.aim_key );
				toggle_row( "Auto Release", n.auto_release );
				settings_popup_row( "Aim Tuning", 3, [ & ]
				{
					slider_row( "Smoothing", n.aim_smoothing, 1, 30 );
					slider_row( "Lock Threshold", n.aim_threshold, 0.05f, 3.0f, "deg", 0.05f );
					slider_row( "Settle Time", n.lock_time_ms, 0, 250, "ms" );
				} );
			} );
		} );

		card_in_column( "grenade_assist", "GRENADE ASSIST", 1, 1, [ & ]
		{
			toggle_popup_row( "Enemy Aim Assist", assist.enabled, 2, [ & ]
			{
				keybind_row( "Activation Key", assist.key );
				settings_popup_row( "Aim Tuning", 2, [ & ]
				{
					slider_row( "Target FOV", assist.fov, 1, 180 );
					slider_row( "Smoothing", assist.smoothing, 1, 50 );
				} );
			} );
		} );

		card_in_column( "trajectory", "TRAJECTORY", 1, 1, [ & ]
		{
			toggle_popup_row( "Trajectory Preview", p.m_grenades.enabled, 4, [ & ]
			{
				toggle_row( "Local Prediction Only", p.m_grenades.local_only );
				settings_popup_row( "Line Style", 3, [ & ]
				{
					color_row( "Line Color", p.m_grenades.color );
					slider_row( "Line Thickness", p.m_grenades.thickness,
						0.5f, 8.0f, "px", 0.25f );
					toggle_popup_row( "Bloom", p.m_grenades.bloom, 2, [ & ]
					{
						color_row( "Bloom Color", p.m_grenades.bloom_color );
						slider_row( "Bloom Radius", p.m_grenades.bloom_radius,
							0.5f, 12.0f, "px", 0.5f );
					} );
				} );
				toggle_popup_row( "Bounce Points", p.m_grenades.show_bounces, 2, [ & ]
				{
					color_row( "Point Color", p.m_grenades.bounce_color );
					slider_row( "Point Size", p.m_grenades.bounce_size,
						1.0f, 16.0f, "px", 0.5f );
				} );
				toggle_popup_row( "End Point", p.m_grenades.show_endpoint, 2, [ & ]
				{
					color_row( "Point Color", p.m_grenades.endpoint_color );
					slider_row( "Point Size", p.m_grenades.endpoint_size,
						2.0f, 24.0f, "px", 0.5f );
				} );
			} );
		} );
	}
	else if ( this->m_misc_group == 2 )
	{
		card( "movement", "MOVEMENT", 3, [ & ]
		{
			toggle_popup_row( "Enable Bunny Hop", p.m_bunny_hop.enabled, 1, [ & ]
			{
				keybind_row( "Activation Key", p.m_bunny_hop.activation_key );
			} );
			toggle_popup_row( "Enable Edge Jump", p.m_edge_jump.enabled, 1, [ & ]
			{
				keybind_row( "Activation Key", p.m_edge_jump.activation_key );
			} );
			toggle_popup_row( "Enable Auto Stop", p.m_auto_stop.enabled, 2, [ & ]
			{
				slider_row( "Stop Speed", p.m_auto_stop.stop_speed,
					0.0f, 150.0f, " u/s", 1.0f );
				slider_row( "Shoot Speed", p.m_auto_stop.required_shoot_speed,
					0.0f, 60.0f, "%", 1.0f );
			} );
		} );
	}
	else if ( this->m_misc_group == 3 )
	{
		card( "overlay", "OVERLAY", 6, [ & ]
		{
			toggle_row( "OBS Bypass", p.obs_bypass );
			toggle_popup_row( "Show Watermark", p.m_watermark.enabled, 9, [ & ]
			{
				toggle_row( "Show Ping", p.m_watermark.show_ping );
				toggle_row( "Show Loss", p.m_watermark.show_loss );
				toggle_row( "Show CPU Load", p.m_watermark.show_cpu );
				toggle_row( "Show FPS", p.m_watermark.show_fps );
				toggle_row( "Show Frame Time", p.m_watermark.show_frame_time );
				toggle_row( "Show Render Time", p.m_watermark.show_render_time );
				toggle_row( "Show Pose Age", p.m_watermark.show_pose_age );
				toggle_row( "Show Refresh Rate", p.m_watermark.show_refresh_rate );
				toggle_row( "Show Present Drops", p.m_watermark.show_present_drops );
			} );
			toggle_popup_row( "Show Spectators", p.m_spectator_list.enabled, 1, [ & ]
			{
				toggle_row( "Show Steam Avatars", p.m_spectator_list.show_avatars );
			} );
			toggle_popup_row( "Show Event Log", p.m_event_log.enabled, 3, [ & ]
			{
				slider_row( "Duration", p.m_event_log.duration,
					0.5f, 20.0f, " s", 0.5f );
				slider_row( "Maximum Entries", p.m_event_log.max_entries, 1, 5 );
				int event_mask = ( p.m_event_log.show_shots ? 1 : 0 )
					| ( p.m_event_log.show_hits ? 2 : 0 )
					| ( p.m_event_log.show_kills ? 4 : 0 )
					| ( p.m_event_log.show_misses ? 8 : 0 )
					| ( p.m_event_log.show_blocked ? 16 : 0 )
					| ( p.m_event_log.show_info ? 32 : 0 );
				static constexpr std::pair<const char*, int> event_options[]{
					{ "Shots", 1 }, { "Hits", 2 }, { "Kills", 4 },
					{ "Misses", 8 }, { "Blocked", 16 }, { "Info", 32 } };
				multiselect_row( "Events", event_mask, event_options, 63 );
				p.m_event_log.show_shots = ( event_mask & 1 ) != 0;
				p.m_event_log.show_hits = ( event_mask & 2 ) != 0;
				p.m_event_log.show_kills = ( event_mask & 4 ) != 0;
				p.m_event_log.show_misses = ( event_mask & 8 ) != 0;
				p.m_event_log.show_blocked = ( event_mask & 16 ) != 0;
				p.m_event_log.show_info = ( event_mask & 32 ) != 0;
			} );
			toggle_popup_row( "Show Active Binds", p.m_keybind_list.enabled, 3, [ & ]
			{
				toggle_row( "Always On Binds", p.m_keybind_list.show_always );
				toggle_row( "Hold Binds", p.m_keybind_list.show_hold );
				toggle_row( "Toggle Binds", p.m_keybind_list.show_toggle );
			} );
			toggle_row( "Show Bomb Info",
				config::visual_settings.m_bomb.show_info_panel );
		} );
	}
	else if ( this->m_misc_group == 4 )
	{
		const auto scripts = scripting::runtime().scripts( );
		card_in_column( "lua_runtime", "LUA RUNTIME", 3, 0, [ & ]
		{
			const auto previous = p.lua_enabled;
			toggle_row( "Enable Lua API", p.lua_enabled );
			if ( p.lua_enabled != previous && p.lua_enabled )
				for ( const auto& script : scripts ) if ( script.autoload )
					scripting::runtime().set_enabled( script.id, true );
			if ( button_row( "Script Directory", "Open Folder", row_action_icon::folder ) )
			{
				const auto path = scripting::runtime().scripts_path().u8string( );
				auto& platform = ImGui::GetPlatformIO( );
				if ( platform.Platform_OpenInShellFn ) platform.Platform_OpenInShellFn(
					ImGui::GetCurrentContext( ), reinterpret_cast<const char*>( path.c_str( ) ) );
			}
			if ( button_row( "Load Lua Script", "Browse...", row_action_icon::folder ) )
				app::context().overlay.request_lua_import( );
		} );

		int script_rows = std::max( 1, static_cast<int>( scripts.size( ) ) );
		for ( const auto& script : scripts )
			if ( _stricmp( script.id.c_str( ), "vesta_web_radar" ) == 0
				&& script.enabled ) ++script_rows;
		card_in_column( "lua_scripts", "SCRIPTS",
			script_rows, 1, [ & ]
		{
			if ( scripts.empty( ) )
			{
				begin_row( "No Lua scripts found", 148.0f );
				clipped_row_text( render::localization::tr( "Copy scripts into the Lua directory" ) );
				end_row( );
			}
			for ( const auto& script : scripts )
			{
				ImGui::PushID( script.id.c_str( ) );
				auto running = script.enabled;
				const auto controls = scripting::runtime().controls( script.id );
				if ( _stricmp( script.id.c_str( ), "vesta_web_radar" ) == 0 )
				{
					toggle_row( script.name.c_str( ), running );
					if ( running )
					{
						for ( const auto& control : controls )
						{
							if ( control.id != "copy" || control.kind != scripting::control_kind::button )
								continue;
							if ( button_row( "Copy link", control.action_text.empty( )
								? "Copy" : control.action_text.c_str( ) ) )
								scripting::runtime().press_control( script.id, control.id );
						}
					}
					if ( running != script.enabled )
						scripting::runtime().set_enabled( script.id, running );
					ImGui::PopID( );
					continue;
				}
				const auto popup_rows = std::clamp( 5 + static_cast<int>( controls.size( ) ), 5, 16 );
				toggle_popup_row( script.name.c_str( ), running, popup_rows, [ & ]
				{
					auto autoload = script.autoload;
					toggle_row( "Autoload", autoload );
					if ( autoload != script.autoload ) scripting::runtime().set_autoload( script.id, autoload );
					auto hot_reload = script.hot_reload;
					toggle_row( "Hot Reload", hot_reload );
					if ( hot_reload != script.hot_reload ) scripting::runtime().set_hot_reload( script.id, hot_reload );

					begin_row( "Status", 116.0f );
					ImGui::TextColored( script.state == scripting::script_state::running
						? ImVec4{ 0.38f, 0.86f, 0.55f, 1.0f }
						: ( script.state == scripting::script_state::error
							|| script.state == scripting::script_state::over_budget )
							? ImVec4{ 1.0f, 0.38f, 0.42f, 1.0f } : k_text_muted,
						"%s", scripting::runtime_t::state_name( script.state ) );
					end_row( );
					begin_row( "Runtime", 116.0f );
					ImGui::TextColored( k_text_muted, "%.2f ms / %.1f MiB",
						script.last_callback_ms,
						static_cast<double>( script.memory_bytes ) / ( 1024.0 * 1024.0 ) );
					end_row( );
					if ( !script.error.empty( ) )
					{
						begin_row( "Last Error", 116.0f );
						clipped_row_text( script.error, ImVec4{ 1.0f, 0.38f, 0.42f, 1.0f } );
						end_row( );
					}

					for ( const auto& control : controls )
					{
						ImGui::PushID( control.id.c_str( ) );
						switch ( control.kind )
						{
						case scripting::control_kind::text:
							begin_row( control.label.c_str( ), 116.0f );
							if ( const auto* value = std::get_if<std::string>( &control.value ) ) clipped_row_text( *value );
							end_row( );
							break;
						case scripting::control_kind::button:
							if ( button_row( control.label.c_str( ), control.action_text.empty( ) ? "Run" : control.action_text.c_str( ) ) )
								scripting::runtime().press_control( script.id, control.id );
							break;
						case scripting::control_kind::toggle:
						{
							auto value = std::get_if<bool>( &control.value ) ? std::get<bool>( control.value ) : false;
							const auto before = value; toggle_row( control.label.c_str( ), value );
							if ( value != before ) scripting::runtime().set_control( script.id, control.id, value );
							break;
						}
						case scripting::control_kind::slider:
						{
							auto value = static_cast<float>( std::get_if<double>( &control.value ) ? std::get<double>( control.value ) : 0.0 );
							const auto before = value;
							slider_row( control.label.c_str( ), value, static_cast<float>( control.minimum ),
								static_cast<float>( control.maximum ), "", static_cast<float>( control.step ) );
							if ( value != before ) scripting::runtime().set_control( script.id, control.id, static_cast<double>( value ) );
							break;
						}
						case scripting::control_kind::select:
						{
							auto value = std::get_if<int>( &control.value ) ? std::get<int>( control.value ) : 0;
							std::vector<const char*> labels{}; labels.reserve( control.options.size( ) );
							for ( const auto& option : control.options ) labels.push_back( option.c_str( ) );
							const auto before = value; select_row( control.label.c_str( ), value, labels );
							if ( value != before ) scripting::runtime().set_control( script.id, control.id, value );
							break;
						}
						case scripting::control_kind::input:
						{
							std::array<char, 512> buffer{};
							if ( const auto* value = std::get_if<std::string>( &control.value ) )
								std::memcpy( buffer.data( ), value->data( ),
									std::min( value->size( ), buffer.size( ) - 1 ) );
							const auto before = std::string( buffer.data( ) );
							text_input_row( control.label.c_str( ), buffer.data( ), buffer.size( ) );
							if ( before != buffer.data( ) ) scripting::runtime().set_control(
								script.id, control.id, std::string( buffer.data( ) ) );
							break;
						}
						case scripting::control_kind::color:
						{
							auto value = std::get_if<zdraw::rgba>( &control.value )
								? std::get<zdraw::rgba>( control.value ) : zdraw::rgba{ 255, 255, 255, 255 };
							const auto before = value.val; color_row( control.label.c_str( ), value );
							if ( before != value.val ) scripting::runtime().set_control( script.id, control.id, value );
							break;
						}
						case scripting::control_kind::keybind:
						{
							auto value = std::get_if<int>( &control.value ) ? std::get<int>( control.value ) : 0;
							const auto before = value; keybind_row( control.label.c_str( ), value );
							if ( before != value ) scripting::runtime().set_control( script.id, control.id, value );
							break;
						}
						case scripting::control_kind::separator:
							begin_row( control.label.c_str( ), 116.0f );
							ImGui::SeparatorText( control.label.c_str( ) );
							end_row( );
							break;
						}
						ImGui::PopID( );
					}
					if ( button_row( "Reload Script", "Reload" ) ) scripting::runtime().reload( script.id );
				} );
				if ( running != script.enabled ) scripting::runtime().set_enabled( script.id, running );
				ImGui::PopID( );
			}
		} );
	}
	else
	{
		card( "menu_hotkey", "MENU", 2, [ & ]
		{
			static int menu_key = platform::windows::lifecycle_keys( ).menu;
			static bool save_failed{};
			const int previous = menu_key;
			keybind_row( "Menu Key", menu_key );
			if ( menu_key != previous )
			{
				// Escape cancels capture; never leave the menu without a key.
				save_failed = menu_key != 0 && !platform::windows::set_menu_key(
					static_cast<std::uint16_t>( menu_key ) );
				menu_key = platform::windows::lifecycle_keys( ).menu;
				this->m_menu_hotkey_was_down = true;
			}
			begin_row( "Status", 230.0f );
			clipped_row_text( save_failed ? "Key unavailable or save failed"
				: "Keyboard key; Esc cancels" );
			end_row( );
		} );
		card( "config", "CFG FILE", 3, [ & ]
			{
				auto& cfg = config::storage;
				text_input_row( "File Name", cfg.name_buffer, sizeof( cfg.name_buffer ) );
				if ( button_row( "Export Named CFG", "Save As...", row_action_icon::save ) )
					app::context().overlay.request_config_save( );
				if ( button_row( "Open CFG", "Browse...", row_action_icon::folder ) )
					app::context().overlay.request_config_load( );
			} );
	}

	end_cards( );
	ImGui::EndChild( );
	ImGui::PopStyleVar( );
}
