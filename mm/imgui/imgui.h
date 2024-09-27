#pragma once

#include <functional>
#include <mutex>
#include <list>
#include <vector>

#include "mm/imgui/imgui/imgui.h"
#include "mm/imgui/imgui/imgui_impl_win32.h"
#include "mm/imgui/imgui/imgui_impl_dx11.h"
#include "mm/imgui/imgui/imgui_detour_dx11.h"

#include "mm/core/hash.h"

namespace ImGui {
	bool IsKeyDown(ImGuiKey key);
	bool IsKeyJustDown(ImGuiKey key);
}

class ImGuiRenderer {
public:

	class Event {
	public:

		Event(CHashString const& _hash) {
			hash = _hash;
			isFunction = false;
		}

		Event(std::function<void()> const& _func) {
			func = _func;
			isFunction = true;
		}

		Event() {
			isFunction = false;
		}

		Event(const Event& _event) {
			isFunction = _event.isFunction;
			if (isFunction)
				func = _event.func;
			else {
				hash = _event.hash;
				args = _event.args;
			}
		}

		~Event() {

		}

		std::function<void()> func;
		CHashString hash;
		std::vector<void*> args;

		bool isFunction;

		template<typename T>
		Event& Add(T arg) {

			if (isFunction)
				return *this;

			void* _arg = nullptr;
			*(T*)&_arg = arg;
			args.push_back(_arg);
			return *this;
		}

		template<typename T>
		T Arg(size_t index = 0) const {
			void* a = nullptr;
			if (!isFunction) {
				a = args[index];	
			}
			return *(T*)&a;
		}
	};

	ImGuiRenderer() { 
		std::lock_guard<std::mutex> lock(Mutex());
		Renderers().push_back(this);
	};

	~ImGuiRenderer() {
		std::lock_guard<std::mutex> lock(Mutex());
		Renderers().remove(this);
	};

	void ProcessEvents(bool isRender = false) {
		Mutex().lock();
		auto copy = isRender ? renderEvents : events;
		Mutex().unlock();

		if (!copy.empty()) {
			for (auto& _event : copy) {
				if (_event.isFunction)
					_event.func();
				else if (!isRender)
					GameHandleEvent(_event);
				else
					RenderHandleEvent(_event);
				Mutex().lock();
				(isRender ? renderEvents : events).pop_front();
				Mutex().unlock();
			}
		}
	};

	virtual void GameHandleEvent(Event const& event) {};
	virtual void Game() {};
	virtual void RenderHandleEvent(Event const& event) {};
	virtual void RenderInit(bool reInit) {};
	virtual void Render() {};
	virtual void RenderShutdown() {};
	
	void SendGameEvent(Event const& _event) {
		std::lock_guard<std::mutex> lock(Mutex());
		events.push_back(_event);
	}

	void SendGameEvent(std::function<void()> const& event_func) {
		std::lock_guard<std::mutex> lock(Mutex());
		events.push_back(Event(event_func));
	}

	void SendRenderEvent(Event const& _event) {
		std::lock_guard<std::mutex> lock(Mutex());
		renderEvents.push_back(_event);
	}

	void SendRenderEvent(std::function<void()> const& event_func) {
		std::lock_guard<std::mutex> lock(Mutex());
		renderEvents.push_back(Event(event_func));
	}
	
	std::list < Event > events, renderEvents;

	inline static std::mutex& Mutex() { 
		static std::mutex mu;
		return mu;
	};

	inline static std::list<ImGuiRenderer*>& Renderers() {
		static std::list<ImGuiRenderer*> renderers;
		return renderers;
	};

	static void OnRenderInit(bool reInit);
	static void OnRenderShutdown();
	static void ProcessGame();
	static void ProcessRender();
	static void Install();
};