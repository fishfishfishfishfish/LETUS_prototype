#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include "LSVPS.hpp"
#include "ddpg_binding.hpp"
#include <iostream>
#include <filesystem>

namespace py = pybind11;

void DDPGBinding::initialize_python() {
    try {
        // Add the Python directory to the path
        py::module_ sys = py::module_::import("sys");
        
        // Get the executable's directory using std::filesystem
        std::filesystem::path exe_path = std::filesystem::current_path();
        std::string exe_dir = exe_path.string();
        
        // Go up one level to get the project root directory
        std::string project_root = exe_dir + "/..";
        std::string py_dir = project_root + "/py";
        
        // Add the py directory to Python's path
        sys.attr("path").attr("insert")(0, py::str(py_dir));
        
        // Print the Python path for debugging
        std::cout << "Executable directory: " << exe_dir << std::endl;
        std::cout << "Project root: " << project_root << std::endl;
        std::cout << "Python path: " << sys.attr("path").attr("__str__")().cast<std::string>() << std::endl;
        
        // Import the DDPG wrapper
        ddpg_module_ = py::module_::import("ddpg_wrapper");
        ddpg_wrapper_ = ddpg_module_.attr("DDPGWrapper")();
    } catch (const py::error_already_set& e) {
        std::string error_msg = "Failed to initialize Python environment: ";
        error_msg += e.what();
        std::cerr << error_msg << std::endl;
        throw std::runtime_error(error_msg);
    }
}

DDPGBinding::DDPGBinding() {
    try {
        initialize_python();
    } catch (const std::exception& e) {
        std::cerr << "Failed to construct DDPGBinding: " << e.what() << std::endl;
        throw;
    }
}

std::vector<float> DDPGBinding::select_action(const std::vector<float>& state) {
    try {
        py::array_t<float> state_array = py::array_t<float>({state.size()}, {sizeof(float)}, state.data());
        py::array_t<float> action = ddpg_wrapper_.attr("select_action")(state_array);
        return std::vector<float>(action.data(), action.data() + action.size());
    } catch (const py::error_already_set& e) {
        std::string error_msg = "Failed to select action: ";
        error_msg += e.what();
        std::cerr << error_msg << std::endl;
        throw std::runtime_error(error_msg);
    }
}

void DDPGBinding::train() {
    try {
        ddpg_wrapper_.attr("train")();
    } catch (const py::error_already_set& e) {
        std::string error_msg = "Failed to train: ";
        error_msg += e.what();
        std::cerr << error_msg << std::endl;
        throw std::runtime_error(error_msg);
    }
}

void DDPGBinding::add_to_replay_buffer(const std::vector<float>& state,
                        const std::vector<float>& action,
                        float reward,
                        const std::vector<float>& next_state,
                        bool done) {
    try {
        py::array_t<float> state_array = py::array_t<float>({state.size()}, {sizeof(float)}, state.data());
        py::array_t<float> action_array = py::array_t<float>({action.size()}, {sizeof(float)}, action.data());
        py::array_t<float> next_state_array = py::array_t<float>({next_state.size()}, {sizeof(float)}, next_state.data());
        
        ddpg_wrapper_.attr("add_to_replay_buffer")(state_array, action_array, reward, next_state_array, done);
    } catch (const py::error_already_set& e) {
        std::string error_msg = "Failed to add to replay buffer: ";
        error_msg += e.what();
        std::cerr << error_msg << std::endl;
        throw std::runtime_error(error_msg);
    }
}

// std::pair<float, float> DDPGBinding::get_state(uint16_t t_b, uint16_t t_d) {
//     try {
//         py::tuple result = ddpg_wrapper_.attr("get_state")(t_b, t_d);
//         return std::make_pair(result[0].cast<float>(), result[1].cast<float>());
//     } catch (const py::error_already_set& e) {
//         std::string error_msg = "Failed to get state: ";
//         error_msg += e.what();
//         std::cerr << error_msg << std::endl;
//         throw std::runtime_error(error_msg);
//     }
// }

double DDPGBinding::get_time_cost(double time_cost) {
    try {
        py::object env = ddpg_wrapper_.attr("env");
        py::object letus_db = env.attr("letus_db");
        return letus_db.attr("get_time_cost")(time_cost, 0).cast<double>();
    } catch (const py::error_already_set& e) {
        std::string error_msg = "Failed to get time cost: ";
        error_msg += e.what();
        std::cerr << error_msg << std::endl;
        throw std::runtime_error(error_msg);
    }
}

double DDPGBinding::get_space_cost(double space_cost) {
    try {
        py::object env = ddpg_wrapper_.attr("env");
        py::object letus_db = env.attr("letus_db");
        return letus_db.attr("get_space_cost")(space_cost, 0).cast<double>();   
    } catch (const py::error_already_set& e) {
        std::string error_msg = "Failed to get space cost: ";
        error_msg += e.what();
        std::cerr << error_msg << std::endl;
        throw std::runtime_error(error_msg);
    }
}

int DDPGBinding::test(double time_cost, double space_cost) {
    try {
        // 初始化环境
        py::module_ gym = py::module_::import("gym");
        py::object env = gym.attr("make")("LetusDB");
        
        // 获取环境参数
        py::object observation_space = env.attr("observation_space");
        py::object action_space = env.attr("action_space");
        
        int state_dim = observation_space.attr("shape").attr("__getitem__")(0).cast<int>();
        int action_dim = action_space.attr("shape").attr("__getitem__")(0).cast<int>();
        float max_action = action_space.attr("high").attr("__getitem__")(0).cast<float>();
        
        // 初始化智能体
        py::object agent = ddpg_module_.attr("DDPGAgent")(state_dim, action_dim, max_action);
        
        // 训练参数
        int episodes = 100;
        int max_steps = 100;
        std::vector<float> rewards;
        
        for (int episode = 0; episode < episodes; episode++) {
            py::tuple reset_result = env.attr("reset")();
            std::vector<float> state = reset_result[0].cast<std::vector<float>>();
            float episode_reward = 0;
            
            for (int step = 0; step < max_steps; step++) {
                // 选择动作
                py::array_t<float> state_array = py::array_t<float>({state.size()}, {sizeof(float)}, state.data());
                py::array_t<float> action = agent.attr("select_action")(state_array);
                
                // 执行动作
                py::tuple step_result = env.attr("step")(action);
                std::vector<float> next_state = step_result[0].cast<std::vector<float>>();
                float reward = step_result[1].cast<float>();
                bool done = step_result[2].cast<bool>();
                
                // 存储经验
                agent.attr("add_to_replay_buffer")(state_array, action, reward, next_state, done);
                
                // 训练智能体
                agent.attr("train")();
                
                state = next_state;
                episode_reward += reward;
                
                if (done) {
                    break;
                }
            }
            
            rewards.push_back(episode_reward);
            std::cout << "Episode: " << episode + 1 << ", Reward: " << episode_reward << std::endl;
        }
        
        // 保存模型
        py::module_ torch = py::module_::import("torch");
        torch.attr("save")(agent.attr("actor").attr("state_dict")(), "ddpg_model_actor.pth");
        torch.attr("save")(agent.attr("critic").attr("state_dict")(), "ddpg_model_critic.pth");
        
        // 计算平均奖励
        float avg_reward = 0;
        for (float reward : rewards) {
            avg_reward += reward;
        }
        avg_reward /= rewards.size();
        std::cout << "Average Reward: " << avg_reward << std::endl;
        
        // 返回最后一个episode的t_b值
        py::tuple last_state = env.attr("get_state")(time_cost, space_cost);
        std::vector<float> final_state = {last_state[0].cast<float>(), last_state[1].cast<float>()};
        py::array_t<float> final_state_array = py::array_t<float>({final_state.size()}, {sizeof(float)}, final_state.data());
        py::array_t<float> final_action = agent.attr("select_action")(final_state_array);
        
        return static_cast<int>(final_action.data()[0]);
        
    } catch (const std::exception& e) {
        std::string error_msg = "Failed in test: ";
        error_msg += e.what();
        std::cerr << error_msg << std::endl;
        return 256;  // 返回默认值
    }
}

// PYBIND11_MODULE(ddpg_binding, m) {
//     py::class_<DDPGBinding>(m, "DDPGBinding")
//         .def(py::init<>())
//         .def("select_action", &DDPGBinding::select_action)
//         .def("train", &DDPGBinding::train)
//         .def("add_to_replay_buffer", &DDPGBinding::add_to_replay_buffer)
//         .def("get_state", &DDPGBinding::get_state)
//         .def("test", &DDPGBinding::test);
// } 