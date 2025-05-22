#pragma once

#include <vector>
#include <utility>
#include <string>
#include <stdexcept>
#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

namespace py = pybind11;

class DDPGBinding {
private:
    // Python interpreter guard must be the first member
    py::scoped_interpreter guard_{};
    
    // Python module and object handles
    py::module_ ddpg_module_;
    py::object ddpg_wrapper_;

    // Helper function to initialize Python environment
    void initialize_python();

public:
    DDPGBinding();
    ~DDPGBinding() = default;  // scoped_interpreter handles cleanup

    // DDPG interface methods
    std::vector<float> select_action(const std::vector<float>& state);
    void train();
    void add_to_replay_buffer(const std::vector<float>& state,
                            const std::vector<float>& action,
                            float reward,
                            const std::vector<float>& next_state,
                            bool done);
    std::pair<float, float> get_state(uint16_t t_b, uint16_t t_d);
    int test(double time_cost, double space_cost);
    double get_time_cost(double time_cost);
    double get_space_cost(double space_cost);
}; 