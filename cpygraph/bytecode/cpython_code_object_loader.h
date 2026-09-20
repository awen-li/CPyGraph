#pragma once

#include "bytecode/code_object_loader.h"

#include <Python.h>

namespace cpygraph::bytecode {

class CPythonCodeObjectLoader : public CodeObjectLoader {
public:
    LoadedCodeObject load(const std::filesystem::path& path) const final;

protected:
    virtual std::string qualifiedName(PyObject* code, const std::string& name) const = 0;
    virtual std::vector<std::string> localNames(PyObject* code) const;

private:
    LoadedCodeObject loadCodeObject(PyObject* code) const;
};

class Python310CodeObjectLoader final : public CPythonCodeObjectLoader {
public:
    std::string_view version() const noexcept override { return "3.10"; }

protected:
    std::string qualifiedName(PyObject*, const std::string& name) const override { return name; }
};

class QualifiedCodeObjectLoader final : public CPythonCodeObjectLoader {
public:
    explicit QualifiedCodeObjectLoader(std::string version) : version_(std::move(version)) {}
    std::string_view version() const noexcept override { return version_; }

protected:
    std::string qualifiedName(PyObject* code, const std::string& name) const override;

private:
    std::string version_;
};

}  // namespace cpygraph::bytecode
