
namespace xlang::meta::reader
{
    struct ElemSig
    {
        struct SystemType
        {
            std::string_view name;
        };

        struct EnumValue
        {
            EnumDefinition type;
            using value_type = std::variant<bool, char16_t, uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t>;
            value_type value;
        };

        using value_type = std::variant<bool, char16_t, uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t, float, double, std::string_view, SystemType, EnumValue>;

        ElemSig(database const& db, ParamSig const& param, byte_view& data)
            : m_value{ read_element(db, param, data) }
        {
        }

        ElemSig(SystemType type)
            : m_value(type)
        {
        }

        ElemSig(EnumDefinition const& enum_def, byte_view& data)
            : m_value{ EnumValue{enum_def, read_enum(enum_def.m_underlying_type, data) } }
        {
        }

        ElemSig(ElementType type, byte_view& data)
            : m_value{ read_primitive(type, data) }
        {
        }

        static value_type read_element(database const& db, ParamSig const& param, byte_view& data)
        {
            auto const& type = param.Type().Type();
            if (auto element_type = std::get_if<ElementType>(&type))
            {
                return read_primitive(*element_type, data);
            }
            else if (auto type_index = std::get_if<coded_index<TypeDefOrRef>>(&type))
            {
                if ((type_index->type() == TypeDefOrRef::TypeRef && type_index->TypeRef().TypeNamespace() == "System" && type_index->TypeRef().TypeName() == "Type") ||
                    type_index->type() == TypeDefOrRef::TypeDef && type_index->TypeDef().TypeNamespace() == "System" && type_index->TypeDef().TypeName() == "Type")
                {
                    return SystemType{ read<std::string_view>(data) };
                }
                else
                {
                    // Should be an enum. Resolve it.
                    auto resolve_type = [&db, &type_index]() -> TypeDef
                    {
                        if (type_index->type() == TypeDefOrRef::TypeDef)
                        {
                            return type_index->TypeDef();
                        }
                        auto const& typeref = type_index->TypeRef();
                        auto const& resolved_type = db.get_cache()->find(typeref.TypeNamespace(), typeref.TypeName());
                        if (resolved_type.has_value())
                        {
                            return *resolved_type;
                        }
                        else
                        {
                            throw_invalid("Unresolved type in CustomAttribute blob");
                        }
                    };
                    TypeDef const& enum_type = resolve_type();
                    if (!enum_type.is_enum())
                    {
                        throw_invalid("CustomAttribute params that are TypeDefOrRef must be an enum or System.Type");
                    }

                    auto const& enum_def = enum_type.get_enum_definition();
                    return EnumValue{ enum_def, read_enum(enum_def.m_underlying_type, data) };
                }
            }
            throw_invalid("Custom attribute params must be primitives, enums, or System.Type");
        }

        static value_type read_primitive(ElementType type, byte_view& data)
        {
            switch (type)
            {
            case ElementType::Boolean:
                return read<bool>(data);

            case ElementType::Char:
                return read<char16_t>(data);

            case ElementType::I1:
                return read<int8_t>(data);

            case ElementType::U1:
                return read<uint8_t>(data);

            case ElementType::I2:
                return read<int16_t>(data);

            case ElementType::U2:
                return read<uint16_t>(data);

            case ElementType::I4:
                return read<int32_t>(data);

            case ElementType::U4:
                return read<uint32_t>(data);

            case ElementType::I8:
                return read<int64_t>(data);

            case ElementType::U8:
                return read<uint64_t>(data);

            case ElementType::R4:
                return read<float>(data);

            case ElementType::R8:
                return read<double>(data);

            case ElementType::String:
                return read<std::string_view>(data);

            default:
                throw_invalid("Non-primitive type encountered");
            }
        }

        static EnumValue::value_type read_enum(ElementType type, byte_view& data)
        {
            switch (type)
            {
            case ElementType::Boolean:
                return read<bool>(data);

            case ElementType::Char:
                return read<char16_t>(data);

            case ElementType::I1:
                return read<int8_t>(data);

            case ElementType::U1:
                return read<uint8_t>(data);

            case ElementType::I2:
                return read<int16_t>(data);

            case ElementType::U2:
                return read<uint16_t>(data);

            case ElementType::I4:
                return read<int32_t>(data);

            case ElementType::U4:
                return read<uint32_t>(data);

            case ElementType::I8:
                return read<int64_t>(data);

            case ElementType::U8:
                return read<uint64_t>(data);

            default:
                throw_invalid("Invalid underling enum type encountered");
            }
        }

        value_type m_value;
    };

    struct FixedArgSig
    {
        using value_type = std::variant<ElemSig, std::vector<ElemSig>>;

        FixedArgSig(database const& db, ParamSig const& ctor_param, byte_view& data)
            : m_value{ read_arg(db, ctor_param, data) }
        {}

        FixedArgSig(ElemSig::SystemType type)
            : m_value{ ElemSig{type} }
        {}

        FixedArgSig(EnumDefinition const& enum_def, byte_view& data)
            : m_value{ ElemSig{ enum_def, data } }
        {}

        FixedArgSig(ElementType type, bool is_array, byte_view& data)
            : m_value{ read_arg(type, is_array, data) }
        {}

        static value_type read_arg(database const& db, ParamSig const& ctor_param, byte_view& data)
        {
            auto const& type_sig = ctor_param.Type();
            if (type_sig.is_szarray())
            {
                std::vector<ElemSig> elems;
                auto const num_elements = read<uint32_t>(data);
                if (num_elements != 0xffffffff)
                {
                    elems.reserve(num_elements);
                    for (uint32_t i = 0; i < num_elements; ++i)
                    {
                        elems.emplace_back(db, ctor_param, data);
                    }
                }
                return elems;
            }
            else
            {
                return ElemSig{ db, ctor_param, data };
            }
        }

        static value_type read_arg(ElementType type, bool is_array, byte_view& data)
        {
            if (is_array)
            {
                std::vector<ElemSig> elems;
                auto const num_elements = read<uint32_t>(data);
                if (num_elements != 0xffffffff)
                {
                    elems.reserve(num_elements);
                    for (uint32_t i = 0; i < num_elements; ++i)
                    {
                        elems.emplace_back(type, data);
                    }
                }
                return elems;
            }
            else
            {
                return ElemSig{ type, data };
            }
        }

        value_type m_value;
    };

    struct NamedArgSig
    {
        NamedArgSig(database const& db, byte_view& data);

        std::string_view m_name;
        FixedArgSig m_value;

    private:
        FixedArgSig parse_value(database const& db, byte_view& data)
        {
            auto const field_or_prop = read<ElementType>(data);
            if (field_or_prop != ElementType::Field && field_or_prop != ElementType::Property)
            {
                throw_invalid("NamedArg must be either FIELD or PROPERTY");
            }
            
            auto type = read<ElementType>(data);
            switch (type)
            {
            case ElementType::Type:
                m_name = read<std::string_view>(data);
                return FixedArgSig{ ElemSig::SystemType{read<std::string_view>(data)} };

            case ElementType::Enum:
            {
                auto type_string = read<std::string_view>(data);
                m_name = read<std::string_view>(data);
                auto const pos = type_string.find('.');
                if (pos == std::string_view::npos)
                {
                    throw_invalid("CustomAttribute param of Enum or System.Type is missing namespace separator");
                }
                auto type_def = db.get_cache()->find(type_string.substr(0, pos), type_string.substr(pos + 1, type_string.size()));
                if (!type_def.has_value())
                {
                    throw_invalid("CustomAttribute named param referenced unresolved enum type");
                }
                if (!type_def->is_enum())
                {
                    throw_invalid("CustomAttribute named param referenced non-enum type");
                }

                return FixedArgSig{ type_def->get_enum_definition(), data };
            }

            default:
            {
                bool const is_array = (type == ElementType::SZArray);
                if (is_array)
                {
                    type = read<ElementType>(data);
                }
                if (type < ElementType::Boolean || ElementType::String < type)
                {
                    throw_invalid("CustomAttribute named param must be a primitive, System.Type, or an Enum");
                }
                m_name = read<std::string_view>(data);
                return FixedArgSig{ type, is_array, data };
            }
            }
        }
    };

    struct CustomAttributeSig
    {
        CustomAttributeSig(table_base const* table, byte_view& data, MethodDefSig const& ctor)
        {
            database const& db = table->get_database();
            auto const prolog = read<uint16_t>(data);
            if (prolog != 0x0001)
            {
                throw_invalid("CustomAttribute blobs must start with prolog of 0x0001");
            }

            for (auto const& param : ctor.Params())
            {
                m_fixed_args.push_back(FixedArgSig{ db, param, data });
            }

            const auto num_named_args = read<uint16_t>(data);
        }

        std::vector<FixedArgSig> const& FixedArgs() const noexcept { return m_fixed_args; }

    private:
        std::vector<FixedArgSig> m_fixed_args;
    };

    inline auto CustomAttribute::Value() const
    {
        auto const ctor = Type();
        MethodDefSig const& method_sig = ctor.type() == CustomAttributeType::MemberRef ? ctor.MemberRef().MethodSignature() : ctor.MethodDef().Signature();
        auto cursor = get_blob(2);
        return CustomAttributeSig{ get_table(), cursor, method_sig };
    }
}
