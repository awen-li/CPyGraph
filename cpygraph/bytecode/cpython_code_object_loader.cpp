#include "bytecode/cpython_code_object_loader.h"

#include <marshal.h>

#include <climits>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace cpygraph::bytecode {
namespace {

constexpr std::size_t kPycMagicSize = sizeof(std::uint32_t);
constexpr std::size_t kPEP552HeaderSize = 16U;

class PyRef {
public:
    explicit PyRef(PyObject* value = nullptr) noexcept : value_(value) {}
    ~PyRef() { Py_XDECREF(value_); }
    PyRef(const PyRef&) = delete;
    PyRef& operator=(const PyRef&) = delete;
    PyObject* get() const noexcept { return value_; }
private:
    PyObject* value_;
};

std::string consumePythonError() {
    if (!PyErr_Occurred()) return {};
    PyObject* type = nullptr;
    PyObject* value = nullptr;
    PyObject* traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);
    PyRef type_ref(type);
    PyRef value_ref(value);
    PyRef traceback_ref(traceback);
    PyRef rendered(value ? PyObject_Str(value) : nullptr);
    if (!rendered.get()) {
        PyErr_Clear();
        return "CPython error could not be rendered";
    }
    Py_ssize_t size = 0;
    const char* text = PyUnicode_AsUTF8AndSize(rendered.get(), &size);
    if (!text) {
        PyErr_Clear();
        return "CPython error string is not valid UTF-8";
    }
    return std::string(text, static_cast<std::size_t>(size));
}

PyRef attribute(PyObject* object, const char* name, bool required = true) {
    PyObject* value = PyObject_GetAttrString(object, name);
    if (!value) {
        if (required) {
            const auto detail = consumePythonError();
            throw std::runtime_error(std::string("code object lacks required attribute ") + name +
                                     (detail.empty() ? "" : ": " + detail));
        }
        PyErr_Clear();
    }
    return PyRef(value);
}

std::string asString(PyObject* value, std::string_view field) {
    if (!value || !PyUnicode_Check(value)) {
        const auto type = value ? Py_TYPE(value)->tp_name : "null";
        throw std::runtime_error("code-object string attribute " + std::string(field) +
                                 " has type " + type);
    }
    Py_ssize_t size = 0;
    const char* text = PyUnicode_AsUTF8AndSize(value, &size);
    if (!text) {
        const auto detail = consumePythonError();
        throw std::runtime_error("code-object string attribute " + std::string(field) +
                                 " is invalid" +
                                 (detail.empty() ? "" : ": " + detail));
    }
    return std::string(text, static_cast<std::size_t>(size));
}

std::optional<std::string> optionalString(PyObject* value) {
    if (!value || !PyUnicode_Check(value)) return std::nullopt;
    Py_ssize_t size = 0;
    const char* text = PyUnicode_AsUTF8AndSize(value, &size);
    if (!text) {
        PyErr_Clear();
        return std::nullopt;
    }
    return std::string(text, static_cast<std::size_t>(size));
}

std::uint32_t asUInt(PyObject* value, std::string_view field) {
    const auto number = PyLong_AsUnsignedLong(value);
    if (PyErr_Occurred()) {
        const auto detail = consumePythonError();
        throw std::runtime_error("code-object integer attribute " + std::string(field) +
                                 " is invalid" +
                                 (detail.empty() ? "" : ": " + detail));
    }
    return static_cast<std::uint32_t>(number);
}

std::vector<std::string> tupleStrings(PyObject* tuple, std::string_view field) {
    if (!PyTuple_Check(tuple)) throw std::runtime_error("code-object name table is not a tuple");
    std::vector<std::string> result;
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(tuple); ++i)
        result.push_back(asString(PyTuple_GET_ITEM(tuple, i), field));
    return result;
}

}  // namespace

LoadedCodeObject CPythonCodeObjectLoader::load(const std::filesystem::path& path) const {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open pyc: " + path.string());
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() <= kPEP552HeaderSize)
        throw std::runtime_error("invalid pyc header: " + path.string());
    const auto runtime_magic = static_cast<unsigned long>(PyImport_GetMagicNumber());
    for (std::size_t index = 0; index < kPycMagicSize; ++index) {
        const auto file_byte = static_cast<unsigned char>(bytes[index]);
        const auto runtime_byte = static_cast<unsigned char>(
            (runtime_magic >> (index * CHAR_BIT)) & UCHAR_MAX);
        if (file_byte != runtime_byte)
            throw std::runtime_error(
                "pyc magic does not match the linked CPython " +
                std::string(version()) + ": " + path.string());
    }
    PyRef root(PyMarshal_ReadObjectFromString(
        bytes.data() + kPEP552HeaderSize,
        static_cast<Py_ssize_t>(bytes.size() - kPEP552HeaderSize)));
    if (!root.get() || !PyCode_Check(root.get())) {
        PyErr_Clear();
        throw std::runtime_error("pyc is incompatible with the selected CPython loader: " + path.string());
    }
    return loadCodeObject(root.get());
}

LoadedCodeObject CPythonCodeObjectLoader::loadCodeObject(PyObject* code) const {
    LoadedCodeObject result;
    auto name = attribute(code, "co_name");
    result.name = asString(name.get(), "co_name");
    result.qualname = qualifiedName(code, result.name);
    result.filename = asString(attribute(code, "co_filename").get(), "co_filename");
    result.argument_count = asUInt(attribute(code, "co_argcount").get(), "co_argcount");
    result.positional_only_argument_count = asUInt(
        attribute(code, "co_posonlyargcount").get(), "co_posonlyargcount");
    result.keyword_only_argument_count = asUInt(
        attribute(code, "co_kwonlyargcount").get(), "co_kwonlyargcount");
    const auto flags = asUInt(attribute(code, "co_flags").get(), "co_flags");
    constexpr std::uint32_t co_varargs = 0x04U;
    constexpr std::uint32_t co_varkeywords = 0x08U;
    result.has_var_arguments = (flags & co_varargs) != 0U;
    result.has_var_keywords = (flags & co_varkeywords) != 0U;
    result.local_count = asUInt(attribute(code, "co_nlocals").get(), "co_nlocals");
    auto code_bytes = attribute(code, "co_code");
    if (!PyBytes_Check(code_bytes.get())) throw std::runtime_error("co_code is not bytes");
    const auto* begin = reinterpret_cast<const std::uint8_t*>(PyBytes_AS_STRING(code_bytes.get()));
    result.bytecode.assign(begin, begin + PyBytes_GET_SIZE(code_bytes.get()));
    result.metadata.names = tupleStrings(attribute(code, "co_names").get(), "co_names");
    result.metadata.locals = localNames(code);
    const auto cell_names = tupleStrings(attribute(code, "co_cellvars").get(), "co_cellvars");
    const auto free_names = tupleStrings(attribute(code, "co_freevars").get(), "co_freevars");
    result.metadata.cell_names = cell_names;
    result.metadata.free_names = free_names;
    auto deref_resolver = attribute(code, "_varname_from_oparg", false);
    if (deref_resolver.get()) {
        const auto locals_plus_bound = result.metadata.locals.size() +
                                       cell_names.size() + free_names.size();
        result.metadata.deref_names.resize(locals_plus_bound);
        for (std::size_t index = 0; index < locals_plus_bound; ++index) {
            PyRef resolved(PyObject_CallFunction(deref_resolver.get(), "k",
                                                 static_cast<unsigned long>(index)));
            if (!resolved.get()) {
                PyErr_Clear();
                continue;
            }
            result.metadata.deref_names[index] = asString(
                resolved.get(), "_varname_from_oparg result");
        }
    } else {
        result.metadata.deref_names = cell_names;
        result.metadata.deref_names.insert(result.metadata.deref_names.end(),
                                           free_names.begin(), free_names.end());
    }

    auto exception_table = attribute(code, "co_exceptiontable", false);
    if (exception_table.get()) {
        if (!PyBytes_Check(exception_table.get()))
            throw std::runtime_error("co_exceptiontable is not bytes");
        const auto* exception_begin = reinterpret_cast<const std::uint8_t*>(
            PyBytes_AS_STRING(exception_table.get()));
        result.metadata.exception_table.assign(
            exception_begin, exception_begin + PyBytes_GET_SIZE(exception_table.get()));
    }

    auto constants = attribute(code, "co_consts");
    if (!PyTuple_Check(constants.get())) throw std::runtime_error("co_consts is not a tuple");
    result.metadata.constant_count = static_cast<std::size_t>(PyTuple_GET_SIZE(constants.get()));
    result.metadata.constant_string_tuples.resize(result.metadata.constant_count);
    result.metadata.constant_strings.resize(result.metadata.constant_count);
    result.metadata.constant_integers.resize(result.metadata.constant_count);
    result.metadata.constant_kinds.resize(result.metadata.constant_count);
    result.constants.reserve(result.metadata.constant_count);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(constants.get()); ++i) {
        auto* constant = PyTuple_GET_ITEM(constants.get(), i);
        auto& constant_kind = result.metadata.constant_kinds[
            static_cast<std::size_t>(i)];
        if (constant == Py_None)
            constant_kind = PythonConstantKind::None;
        else if (constant == Py_NotImplemented)
            constant_kind = PythonConstantKind::NotImplemented;
        else if (PyBool_Check(constant))
            constant_kind = PythonConstantKind::Boolean;
        if (PyLong_Check(constant)) {
            const auto value = PyLong_AsLongLong(constant);
            if (PyErr_Occurred()) {
                PyErr_Clear();
            } else {
                result.metadata.constant_integers[static_cast<std::size_t>(i)] = value;
            }
        }
        if (PyTuple_Check(constant)) {
            auto& strings = result.metadata.constant_string_tuples[
                static_cast<std::size_t>(i)];
            bool all_strings = true;
            for (Py_ssize_t item = 0; item < PyTuple_GET_SIZE(constant); ++item) {
                auto* value = PyTuple_GET_ITEM(constant, item);
                const auto text = optionalString(value);
                if (!text) {
                    all_strings = false;
                    break;
                }
                strings.push_back(*text);
            }
            if (!all_strings) strings.clear();
        }
        result.metadata.constant_strings[static_cast<std::size_t>(i)] =
            optionalString(constant);
        if (PyCode_Check(constant)) {
            const auto child_index = result.nested.size();
            result.nested.push_back(loadCodeObject(constant));
            result.constants.push_back({LoadedConstant::Kind::CodeObject,
                                        result.nested.back().qualname, child_index,
                                        std::nullopt});
        } else {
            result.constants.push_back({LoadedConstant::Kind::Other, "constant", 0,
                                        optionalString(constant)});
        }
    }
    return result;
}

std::vector<std::string> CPythonCodeObjectLoader::localNames(PyObject* code) const {
    return tupleStrings(attribute(code, "co_varnames").get(), "co_varnames");
}

std::string QualifiedCodeObjectLoader::qualifiedName(PyObject* code, const std::string& name) const {
    auto qualname = attribute(code, "co_qualname", false);
    return qualname.get() ? asString(qualname.get(), "co_qualname") : name;
}

std::unique_ptr<CodeObjectLoader> CodeObjectLoaderFactory::forVersion(std::string_view version) {
    if (version == "3.10") return std::make_unique<Python310CodeObjectLoader>();
    if (version == "3.11" || version == "3.12" || version == "3.13" || version == "3.14")
        return std::make_unique<QualifiedCodeObjectLoader>(std::string(version));
    throw std::invalid_argument("unsupported CPython code-object version: " + std::string(version));
}

std::unique_ptr<CodeObjectLoader> CodeObjectLoaderFactory::forCurrentRuntime() {
    return forVersion(std::to_string(PY_MAJOR_VERSION) + "." + std::to_string(PY_MINOR_VERSION));
}

}  // namespace cpygraph::bytecode
