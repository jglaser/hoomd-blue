// Copyright (c) 2009-2021 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: pschoenhoefer

#include "ManifoldPrimitive.h"

//! Exports the Primitive manifold class to python
void export_ManifoldPrimitive(pybind11::module& m)
    {
    pybind11::class_< ManifoldPrimitive, std::shared_ptr<ManifoldPrimitive> >(m, "ManifoldPrimitive")
    .def(pybind11::init<int, int, int, Scalar >())
    .def("implicit_function", &ManifoldPrimitive::implicit_function)
    .def("derivative", &ManifoldPrimitive::derivative)
    ;
    }
