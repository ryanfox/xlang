#pragma once

namespace xlang
{
    static void write_component_override_defaults(writer& w, TypeDef const& type)
    {
        std::vector<std::string> interfaces;

        for (auto&& base : get_bases(type))
        {
            if (settings.filter.includes(base))
            {
                continue;
            }

            for (auto&&[name, info] : get_interfaces(w, base))
            {
                if (info.base)
                {
                    continue;
                }

                if (info.overridable)
                {
                    interfaces.push_back(name);
                }
            }
        }

        bool first{ true };

        for (auto&& name : interfaces)
        {
            if (first)
            {
                first = false;
                w.write(",\n        %T<D>", name);
            }
            else
            {
                w.write(", %T<D>", name);
            }
        }
    }

    static void write_component_class_base(writer& w, TypeDef const& type)
    {
        bool first{ true };

        for (auto&& base : get_bases(type))
        {
            if (settings.filter.includes(base))
            {
                continue;
            }

            if (first)
            {
                first = false;
                w.write(",\n    impl::base<D");
            }

            w.write(", %", base);
        }

        if (!first)
        {
            w.write('>');
        }
    }

    static void write_component_include(writer& w, TypeDef const& type)
    {
        if (!has_factory_members(type))
        {
            return;
        }

        if (settings.component_opt)
        {
            auto format = R"(void* winrt_make_%();
)";

            w.write(format, get_impl_name(type.TypeNamespace(), type.TypeName()));
        }
        else
        {
            auto format = R"(#include "%.h"
)";

            w.write(format, get_component_filename(type));
        }
    }

    static void write_component_activation(writer& w, TypeDef const& type)
    {
        if (!has_factory_members(type))
        {
            return;
        }

        auto type_name = type.TypeName();
        auto type_namespace = type.TypeNamespace();
        auto impl_name = get_impl_name(type_namespace, type_name);

        if (settings.component_opt)
        {
            auto format = R"(
    if (requal(name, L"%.%"))
    {
        return winrt_make_%();
    }
)";

            w.write(format,
                type_namespace,
                type_name,
                impl_name);
        }
        else
        {
            auto format = R"(
    if (requal(name, L"%.%"))
    {
        return winrt::detach_abi(winrt::make<winrt::@::factory_implementation::%>());
    }
)";

            w.write(format,
                type_namespace,
                type_name,
                type_namespace,
                type_name);
        }
    }

    static void write_module_g_cpp(writer& w, std::vector<TypeDef> const& classes)
    {
        auto format = R"(#include "winrt/base.h"
%
bool WINRT_CALL %_can_unload_now() noexcept
{
    if (winrt::get_module_lock())
    {
        return false;
    }

    winrt::clear_factory_cache();
    return true;
}

void* WINRT_CALL %_get_activation_factory(std::wstring_view const& name)
{
    auto requal = [](std::wstring_view const& left, std::wstring_view const& right) noexcept
    {
        return std::equal(left.rbegin(), left.rend(), right.rbegin(), right.rend());
    };
%
    return nullptr;
}

int32_t WINRT_CALL WINRT_CanUnloadNow() noexcept
{
#ifdef _WRL_MODULE_H_
    if (!::Microsoft::WRL::Module<::Microsoft::WRL::InProc>::GetModule().Terminate())
    {
        return 1;
    }
#endif

    return %_can_unload_now() ? 0 : 1;
}

int32_t WINRT_CALL WINRT_GetActivationFactory(void* classId, void** factory) noexcept
{
    try
    {
        uint32_t length{};
        wchar_t const* const buffer = WINRT_WindowsGetStringRawBuffer(classId, &length);
        std::wstring_view const name{ buffer, length };

        *factory = %_get_activation_factory(name);

        if (*factory)
        {
            return 0;
        }

#ifdef _WRL_MODULE_H_
        return ::Microsoft::WRL::Module<::Microsoft::WRL::InProc>::GetModule().GetActivationFactory(static_cast<HSTRING>(classId), reinterpret_cast<::IActivationFactory**>(factory));
#else
        return winrt::hresult_class_not_available(name).to_abi();
#endif
    }
    catch (...) { return winrt::to_hresult(); }
}
)";

        w.write(format,
            bind_each<write_component_include>(classes),
            settings.component_lib,
            settings.component_lib,
            bind_each<write_component_activation>(classes),
            settings.component_lib,
            settings.component_lib);
    }

    static void write_component_interfaces(writer& w, TypeDef const& type)
    {
        if (is_fast_class(type))
        {
            w.write(", fast_interface<@::%>", type.TypeNamespace(), type.TypeName());

            for (auto&&[interface_name, info] : get_interfaces(w, type))
            {
                if (!is_exclusive(info.type) && !info.base)
                {
                    w.write(", @", interface_name);
                }
            }
        }
        else
        {
            for (auto&&[interface_name, info] : get_interfaces(w, type))
            {
                if (!info.base)
                {
                    w.write(", @", interface_name);
                }
            }
        }

        if (has_composable_constructors(type))
        {
            w.write(", composable");
        }

        auto base_type = get_base_class(type);

        if (!base_type)
        {
            return;
        }

        if (settings.filter.includes(base_type))
        {
            return;
        }

        w.write(", composing");

        for (auto&&[interface_name, info] : get_interfaces(w, base_type))
        {
            if (info.overridable)
            {
                w.write(", @", interface_name);
            }
        }
    }

    static void write_component_composable_forwarder(writer& w, MethodDef const& method)
    {
        auto format = R"(        % %(%)
        {
            return impl::composable_factory<T>::template CreateInstance<%>(%);
        }
)";

        method_signature signature{ method };
        method_signature reordered_method = signature;
        auto&& params = reordered_method.params();
        std::rotate(params.begin(), params.end() - 2, params.end());
        w.param_names = true;

        w.write(format,
            signature.return_signature(),
            get_name(method),
            bind<write_implementation_params>(signature),
            signature.return_signature(),
            bind<write_consume_args>(reordered_method));
    }

    static void write_component_constructor_forwarder(writer& w, MethodDef const& method)
    {
        auto format = R"(        % %(%)
        {
            return make<T>(%);
        }
)";

        method_signature signature{ method };
        w.param_names = true;

        w.write(format,
            signature.return_signature(),
            get_name(method),
            bind<write_implementation_params>(signature),
            bind<write_consume_args>(signature));
    }

        void write_component_static_forwarder(writer& w, MethodDef const& method)
    {
        auto format = R"(        % %(%)
        {
            return T::%(%);
        }
)";

        method_signature signature{ method };
        w.param_names = true;

        w.write(format,
            signature.return_signature(),
            get_name(method),
            bind<write_implementation_params>(signature),
            get_name(method),
            bind<write_consume_args>(signature));
    }

    static void write_component_forwarders(writer& w, std::vector<factory_type> const& factories)
    {
        bool default_constructor{};

        for (auto&& factory : factories)
        {
            if (factory.activatable)
            {
                if (!factory.type)
                {
                    default_constructor = true;

                    w.write(R"(        Windows::Foundation::IInspectable ActivateInstance() const
        {
            return make<T>();
        }
)");
                }
                else
                {
                    w.write_each<write_component_constructor_forwarder>(factory.type.MethodList());
                }
            }
            else if (factory.statics)
            {
                w.write_each<write_component_static_forwarder>(factory.type.MethodList());
            }
            else if (factory.composable)
            {
                w.write_each<write_component_composable_forwarder>(factory.type.MethodList());
            }
        }

        if (!default_constructor)
        {
            w.write(R"(        [[noreturn]] Windows::Foundation::IInspectable ActivateInstance() const
        {
            throw hresult_not_implemented();
        }
)");
        }
    }

    static void write_component_factory_interfaces(writer& w, std::vector<factory_type> const& factories)
    {
        for (auto&& factory : factories)
        {
            if (!factory.type)
            {
                continue;
            }
            
            w.write(", %", factory.type);
        }
    }

    static void write_component_g_cpp(writer& w, TypeDef const& type)
    {
        auto type_name = type.TypeName();
        auto type_namespace = type.TypeNamespace();
        auto impl_name = get_impl_name(type_namespace, type_name);

        if (has_factory_members(type))
        {
            auto format = R"(
void* winrt_make_%()
{
    return winrt::detach_abi(winrt::make<winrt::@::factory_implementation::%>());
}
)";

            w.write(format,
                impl_name,
                type_namespace,
                type_name);
        }

        if (!settings.component_opt)
        {
            return;
        }

        write_type_namespace(w, type_namespace);

        for (auto&& factory : get_factories(type))
        {
            if (factory.activatable)
            {
                if (!factory.type)
                {
                    auto format = R"(    %::%() :
        %(make<@::implementation::%>())
    {
    }
)";

                    w.write(format,
                        type_name,
                        type_name,
                        type_name,
                        type_namespace,
                        type_name);
                }
                else
                {
                    for (auto&& method : factory.type.MethodList())
                    {
                        method_signature signature{ method };

                        auto format = R"(    %::%(%) :
        %(make<@::implementation::%>(%))
    {
    }
)";

                        w.write(format,
                            type_name,
                            type_name,
                            bind<write_consume_params>(signature),
                            type_name,
                            type_namespace,
                            type_name,
                            bind<write_consume_args>(signature));
                    }
                }
            }
            else if (factory.composable && factory.visible)
            {
                for (auto&& method : factory.type.MethodList())
                {
                    method_signature signature{ method };
                    auto& params = signature.params();
                    params.pop_back();
                    params.pop_back();

                    auto format = R"(    %::%(%) :
        %(make<@::implementation::%>(%))
    {
    }
)";

                    w.write(format,
                        type_name,
                        type_name,
                        bind<write_consume_params>(signature),
                        type_name,
                        type_namespace,
                        type_name,
                        bind<write_consume_args>(signature));
                }
            }
            else if (factory.statics)
            {
                for (auto&& method : factory.type.MethodList())
                {
                    method_signature signature{ method };
                    auto method_name = get_name(method);
                    w.async_types = is_async(method, signature);

                    if (is_add_overload(method) || is_remove_overload(method))
                    {
                        auto format = R"(    % %::%(%)
    {
        auto f = make<winrt::@::factory_implementation::%>().as<%>();
        return f.%(%);
    }
)";


                        w.write(format,
                            signature.return_signature(),
                            type_name,
                            method_name,
                            bind<write_consume_params>(signature),
                            type_namespace,
                            type_name,
                            factory.type,
                            method_name,
                            bind<write_consume_args>(signature));
                    }
                    else
                    {
                        auto format = R"(    % %::%(%)
    {
        return @::implementation::%::%(%);
    }
)";


                        w.write(format,
                            signature.return_signature(),
                            type_name,
                            method_name,
                            bind<write_consume_params>(signature),
                            type_namespace,
                            type_name,
                            method_name,
                            bind<write_consume_args>(signature));
                    }

                    if (is_add_overload(method))
                    {
                        auto format = R"(    %::%_revoker %::%(auto_revoke_t, %)
    {
        auto f = make<winrt::@::factory_implementation::%>().as<%>();
        return { f, f.%(%) };
    }
)";

                        w.write(format,
                            type_name,
                            method_name,
                            type_name,
                            method_name,
                            bind<write_consume_params>(signature),
                            type_namespace,
                            type_name,
                            factory.type,
                            method_name,
                            bind<write_consume_args>(signature));
                    }
                }
            }
        }

        if (!is_fast_class(type))
        {
            write_close_namespace(w);
            return;
        }

        for (auto&& info : get_fast_interfaces(w, type))
        {
            for (auto&& method : info.methods)
            {
                auto method_name = get_name(method);
                method_signature signature{ method };
                w.async_types = is_async(method, signature);

                std::string_view format;

                if (is_noexcept(method))
                {
                    format = R"(    % %::%(%) const noexcept
    {
        return get_self<@::implementation::%>(*this)->%(%);
    }
)";
                }
                else
                {
                    format = R"(    % %::%(%) const
    {
        return get_self<@::implementation::%>(*this)->%(%);
    }
)";
                }

                w.write(format,
                    signature.return_signature(),
                    type_name,
                    method_name,
                    bind<write_consume_params>(signature),
                    type_namespace,
                    type_name,
                    method_name,
                    bind<write_consume_args>(signature));

                if (is_add_overload(method))
                {
                    format = R"(    %::%_revoker %::%(auto_revoke_t, %) const
    {
        return impl::make_event_revoker<%, %_revoker>(this, %(%));
    }
)";

                    w.write(format,
                        type_name,
                        method_name,
                        type_name,
                        method_name,
                        bind<write_consume_params>(signature),
                        method_name,
                        method_name,
                        bind<write_consume_args>(signature));
                }
            }
        }

        write_close_namespace(w);
    }

    static void write_component_override_dispatch_base(writer& w, TypeDef const& type)
    {
        if (!is_composable(type))
        {
            return;
        }

        std::string interfaces;

        for (auto&& [name, info] : get_interfaces(w, type))
        {
            if (!info.overridable)
            {
                continue;
            }

            interfaces += ", ";
            interfaces += name;
        }

        if (interfaces.empty())
        {
            return;
        }

        auto format = R"(
    protected:
        using dispatch = impl::dispatch_to_overridable<D@>;
        auto overridable() noexcept { return dispatch::overridable(static_cast<D&>(*this)); }
        )";

        w.write(format, interfaces);
    }

    static void write_component_class_override_constructors(writer& w, TypeDef const& type)
    {
        auto base_type = get_base_class(type);

        if (!base_type)
        {
            return;
        }

        if (settings.filter.includes(base_type))
        {
            return;
        }

        auto type_name = type.TypeName();

        for (auto&& factory : get_factories(base_type))
        {
            if (!factory.composable)
            {
                continue;
            }

            for (auto&& method : factory.type.MethodList())
            {
                method_signature signature{ method };
                auto& params = signature.params();
                params.resize(params.size() - 2);

                auto format = R"(
    %_base(%)
    {
        impl::call_factory<%, %>([&](auto&& f) { f.%(%%*this, this->m_inner); });
    }
                )";

                w.write(format,
                    type_name,
                    bind<write_consume_params>(signature),
                    base_type,
                    factory.type,
                    get_name(method),
                    bind<write_consume_args>(signature),
                    params.empty() ? "" : ", ");
            }
        }
    }

    static void write_component_g_h(writer& w, TypeDef const& type)
    {
        auto type_name = type.TypeName();
        auto type_namespace = type.TypeNamespace();
        auto interfaces = get_interfaces(w, type);
        auto factories = get_factories(type);
        bool const non_static = !empty(type.InterfaceImpl());

        if (non_static)
        {
            auto format = R"(namespace winrt::@::implementation
{
    template <typename D%, typename... I>
    struct WINRT_EBO %_base : implements<D%%, %I...>%%%
    {
        using base_type = %_base;
        using class_type = @::%;
        using implements_type = typename %_base::implements_type;
        using implements_type::implements_type;
        %
        operator impl::producer_ref<class_type> const() const noexcept
        {
            return { to_abi<default_interface<class_type>>(this) };
        }

        hstring GetRuntimeClassName() const
        {
            return L"%.%";
        }
    %%};
}
)";

            auto base_type = get_base_class(type);
            std::string composable_base_name;
            std::string base_type_parameter;
            std::string base_type_argument;
            std::string no_module_lock;
            bool external_base_type{};

            if (base_type)
            {
                external_base_type = !settings.filter.includes(base_type);

                if (external_base_type)
                {
                    composable_base_name = w.write_temp("using composable_base = %;", base_type);
                }
                else
                {
                    base_type_parameter = ", typename B";
                    base_type_argument = ", B";
                    no_module_lock = "no_module_lock, ";
                }
            }

            w.write(format,
                type_namespace,
                base_type_parameter,
                type_name,
                bind<write_component_interfaces>(type),
                base_type_argument,
                no_module_lock,
                "",
                bind<write_component_class_base>(type),
                bind<write_component_override_defaults>(type),
                type_name,
                type_namespace,
                type_name,
                type_name,
                composable_base_name,
                type_namespace,
                type_name,
                bind<write_component_class_override_constructors>(type),
                bind<write_component_override_dispatch_base>(type));
        }

        if (has_factory_members(type))
        {
            auto format = R"(namespace winrt::@::factory_implementation
{
    template <typename D, typename T, typename... I>
    struct WINRT_EBO %T : implements<D, Windows::Foundation::IActivationFactory%, I...>
    {
        using instance_type = @::%;

        hstring GetRuntimeClassName() const
        {
            return L"%.%";
        }
%    };
}
)";

            w.write(format,
                type_namespace,
                type_name,
                bind<write_component_factory_interfaces>(factories),
                type_namespace,
                type_name,
                type_namespace,
                type_name,
                bind<write_component_forwarders>(factories));
        }

        if (non_static)
        {
            auto format = R"(
#if defined(WINRT_FORCE_INCLUDE_%_XAML_G_H) || __has_include("%.xaml.g.h")
#include "%.xaml.g.h"
#else

namespace winrt::@::implementation
{
    template <typename D, typename... I>
    using %T = %_base<D, I...>;
}

#endif
)";

            std::string upper(type_name);
            std::transform(upper.begin(), upper.end(), upper.begin(), [](char c) {return static_cast<char>(::toupper(c)); });

            auto include_path = get_generated_component_filename(type);

            w.write(format,
                upper,
                include_path,
                include_path,
                type_namespace,
                type_name,
                type_name);
        }
    }

    static void write_component_base(writer& w, TypeDef const& type)
    {
        if (empty(type.InterfaceImpl()))
        {
            return;
        }

        auto type_name = type.TypeName();
        auto base_type = get_base_class(type);

        if (base_type && settings.filter.includes(base_type))
        {
            w.write(" : %T<%, @::implementation::%>",
                type_name,
                type_name,
                base_type.TypeNamespace(),
                base_type.TypeName());
        }
        else
        {
            w.write(" : %T<%>", type_name, type_name);
        }
    }

    static void write_component_member_declarations(writer& w, TypeDef const& type)
    {
        auto type_name = type.TypeName();

        for (auto&& factory : get_factories(type))
        {
            if (factory.activatable)
            {
                if (!factory.type)
                {
                    continue;
                }

                for (auto&& method : factory.type.MethodList())
                {
                    method_signature signature{ method };

                    w.write("        %(%);\n",
                        type_name,
                        bind<write_implementation_params>(signature));
                }
            }
            else if (factory.statics)
            {
                for (auto&& method : factory.type.MethodList())
                {
                    method_signature signature{ method };
                    w.async_types = is_async(method, signature);
                    auto method_name = get_name(method);

                    w.write("        static % %(%)%;\n",
                        signature.return_signature(),
                        method_name,
                        bind<write_implementation_params>(signature),
                        is_noexcept(method) ? " noexcept" : "");
                }
            }
        }

        for (auto&&[interface_name, info] : get_interfaces(w, type))
        {
            if (info.base)
            {
                continue;
            }

            w.generic_param_stack.insert(w.generic_param_stack.end(), info.generic_param_stack.begin(), info.generic_param_stack.end());

            for (auto&& method : info.type.MethodList())
            {
                method_signature signature{ method };
                w.async_types = is_async(method, signature);
                auto method_name = get_name(method);

                w.write("        % %(%)%;\n",
                    signature.return_signature(),
                    method_name,
                    bind<write_implementation_params>(signature),
                    is_noexcept(method) ? " noexcept" : "");
            }

            w.generic_param_stack.resize(w.generic_param_stack.size() - info.generic_param_stack.size());
        }
    }

    static void write_component_h(writer& w, TypeDef const& type)
    {
        auto type_name = type.TypeName();
        auto type_namespace = type.TypeNamespace();
        auto base_type = get_base_class(type);
        std::string base_include;

        if (base_type)
        {
            if (settings.filter.includes(base_type))
            {
                base_include = "#include \"" + get_generated_component_filename(base_type) + ".h\"\n";
            }
        }

        {
            auto format = R"(#include "%.g.h"
%
namespace winrt::@::implementation
{
    struct %%
    {
        %() = default;

%    };
}
)";

            w.write(format,
                get_generated_component_filename(type),
                base_include,
                type_namespace,
                type_name,
                bind<write_component_base>(type),
                type_name,
                bind<write_component_member_declarations>(type));

        }

        if (has_factory_members(type))
        {
            auto format = R"(namespace winrt::@::factory_implementation
{
    struct % : %T<%, implementation::%>
    {
    };
}
)";
            w.write(format,
                type_namespace,
                type_name,
                type_name,
                type_name,
                type_name);
        }
    }

    static void write_component_member_definitions(writer& w, TypeDef const& type)
    {
        auto type_name = type.TypeName();

        for (auto&& factory : get_factories(type))
        {
            if (factory.activatable)
            {
                if (!factory.type)
                {
                    continue;
                }

                auto format = R"(    %::%(%)
    {
        throw hresult_not_implemented();
    }
)";

                for (auto&& method : factory.type.MethodList())
                {
                    method_signature signature{ method };

                    w.write(format,
                        type_name,
                        type_name,
                        bind<write_implementation_params>(signature));
                }
            }
            else if (factory.statics)
            {
                auto format = R"(    % %::%(%)%
    {
        throw hresult_not_implemented();
    }
)";

                for (auto&& method : factory.type.MethodList())
                {
                    method_signature signature{ method };
                    w.async_types = is_async(method, signature);
                    auto method_name = get_name(method);

                    w.write(format,
                        signature.return_signature(),
                        type_name,
                        method_name,
                        bind<write_implementation_params>(signature),
                        is_noexcept(method) ? " noexcept" : "");
                }
            }
        }

        for (auto&&[interface_name, info] : get_interfaces(w, type))
        {
            if (info.base)
            {
                continue;
            }

            w.generic_param_stack.insert(w.generic_param_stack.end(), info.generic_param_stack.begin(), info.generic_param_stack.end());

            for (auto&& method : info.type.MethodList())
            {
                auto format = R"(    % %::%(%)%
    {
        throw hresult_not_implemented();
    }
)";

                method_signature signature{ method };
                w.async_types = is_async(method, signature);
                auto method_name = get_name(method);

                w.write(format,
                    signature.return_signature(),
                    type_name,
                    method_name,
                    bind<write_implementation_params>(signature),
                    is_noexcept(method) ? " noexcept" : "");
            }

            w.generic_param_stack.resize(w.generic_param_stack.size() - info.generic_param_stack.size());
        }
    }

    static void write_component_cpp(writer& w, TypeDef const& type)
    {
        auto format = R"(#include "%.h"

namespace winrt::@::implementation
{
%}
)";

        w.write(format,
            get_component_filename(type),
            type.TypeNamespace(),
            bind<write_component_member_definitions>(type));
    }
}