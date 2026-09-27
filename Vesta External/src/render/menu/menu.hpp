#pragma once

#include <windows.h>

#include <atomic>

struct ImVec2;

class menu_t
{
public:
	bool initialize( HWND hwnd );
	void poll_hotkey( );
	[[nodiscard]] bool binding_key( ) const;
	void draw( );
	void map_pointer_to_layout(
		float& x, float& y, float display_width, float display_height ) const noexcept;
	void map_pointer_to_screen(
		float& x, float& y, float display_width, float display_height ) const noexcept;
	void close( ) noexcept { this->m_open.store( false, std::memory_order_release ); }
	[[nodiscard]] bool is_open( ) const noexcept
	{
		return this->m_open.load( std::memory_order_acquire );
	}

private:
	void draw_tabbar( ImVec2 pos, ImVec2 size );
	void draw_content( );
	void draw_esp_toggle( ImVec2 pos, ImVec2 size );
	void draw_visual_editor( );
	void draw_combat( bool triggerbot );
	void draw_visuals( );
	void draw_misc( );
	void reset_content_animation( );
	// Tabs: aimbot, triggerbot, visuals, misc, configs (misc group 5), settings (misc group 3).
	void select_tab( int tab );
	[[nodiscard]] int active_tab( ) const noexcept;

	std::atomic<bool> m_open{};
	bool m_menu_hotkey_was_down{};
	bool m_open_pending{};
	std::chrono::steady_clock::time_point m_last_toggle_time{};
	HWND m_hwnd{};
	int m_page{};
	int m_weapon_group{ -1 };
	int m_visual_group{};
	int m_misc_group{};
	int m_last_misc_group{};
	float m_menu_alpha{};
	float m_tab_transition{ 1.0f };
	bool m_esp_preview_open{ true };
	float m_esp_toggle_hover{};
	float m_visual_editor_animation{};
};
