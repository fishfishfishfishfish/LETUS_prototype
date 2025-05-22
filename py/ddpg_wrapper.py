import numpy as np
from DDPG import DDPGAgent, DBEnv

class DDPGWrapper:
    def __init__(self, state_dim=2, action_dim=2, max_action=65535):
        self.env = DBEnv()
        self.agent = DDPGAgent(state_dim, action_dim, max_action)
        self.last_state = None
        self.last_action = None
        
    def select_action(self, state):
        """Select action based on current state"""
        state_array = np.array(state, dtype=np.float32)
        action = self.agent.select_action(state_array)
        self.last_state = state_array
        self.last_action = action
        return action
    
    def train(self):
        """Train the agent"""
        self.agent.train()
        
    def add_to_replay_buffer(self, state, action, reward, next_state, done):
        """Add experience to replay buffer"""
        state_array = np.array(state, dtype=np.float32)
        action_array = np.array(action, dtype=np.float32)
        next_state_array = np.array(next_state, dtype=np.float32)
        self.agent.add_to_replay_buffer(state_array, action_array, reward, next_state_array, done)
        
    def get_state(self, t_b, t_d):
        """Get state from environment"""
        return self.env.letus_db.get_state(t_b, t_d)
        
    def update_reward(self, time_cost, space_cost):
        """Update reward based on time and space cost"""
        if self.last_state is not None and self.last_action is not None:
            # 计算奖励：负的时间开销和空间开销的加权和
            reward = -(time_cost + space_cost)
            # 获取新的状态
            next_state = np.array([time_cost, space_cost], dtype=np.float32)
            # 将经验添加到回放池
            self.add_to_replay_buffer(self.last_state, self.last_action, reward, next_state, False)
            # 训练智能体
            self.train()
            # 更新状态
            self.last_state = next_state 