import time
import numpy as np
from DDPG import DDPGAgent, DBEnv

def main():
    start_time = time.time()
    
    # 创建环境
    env = DBEnv()
    state_dim = env.observation_space.shape[0]
    action_dim = env.action_space.shape[0]
    max_action = float(env.action_space.high[0])
    
    # 初始化智能体
    agent = DDPGAgent(state_dim, action_dim, max_action)
    
    # 测试参数
    time_cost = 1.0
    space_cost = 100.0
    
    # 获取状态
    state = np.array([time_cost, space_cost], dtype=np.float32)
    
    # 选择动作
    action = agent.select_action(state)
    print(f"Selected action: {action}")
    
    end_time = time.time()
    execution_time = (end_time - start_time) * 1_000_000  # 转换为微秒
    print(f"Standalone Python execution time: {execution_time:.2f} microseconds")

if __name__ == "__main__":
    main() 