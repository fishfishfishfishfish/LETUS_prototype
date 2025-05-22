import gym  # 导入 Gym 库，用于创建和管理强化学习环境
from gym import spaces
import numpy as np  # 导入 NumPy，用于处理数组和数学运算
import torch  # 导入 PyTorch，用于构建和训练神经网络
import torch.nn as nn  # 导入 PyTorch 的神经网络模块
import torch.optim as optim  # 导入 PyTorch 的优化器模块
from collections import deque  # 导入双端队列，用于实现经验回放池
import random  # 导入随机模块，用于从经验池中采样
from typing import Any
import mmap

class DBEnv(gym.Env):
    def __init__(self):
        super(DBEnv, self).__init__()
        self.action_space = spaces.Box(low = 1, high = 65535, shape = [2,], dtype = np.uint16)  # 动作空间，2个连续动作，代表调整tb和td的值
        self.observation_space = spaces.Box(low = 0, high = np.inf, shape = [2,], dtype = np.float32) # 状态空间，2个连续状态，代表数据库时间开销和空间开销
        self.state = np.array([0.0, 0.0])  # 初始化状态
        self.letus_db = LetusDB()  # 假设有一个数据库对象，用于获取当前状态

    def reset(self) -> tuple:
        # 重置环境，返回初始状态和信息字典
        self.state = np.array([0, 0])
        self.letus_db.reset()  # 重置数据库状态
        return self.state.copy(), {}

    def step(self, action):
        # 执行动作，返回下一个状态、奖励、完成标志、信息字典和其他信息
        t_b, t_d = action
        time_cost, space_cost = self.letus_db.get_state(t_b, t_d)  # 获取当前状态，假设有一个函数可以获取数据库的当前状态
        self.state = np.array([time_cost, space_cost])
        k1 = 1
        k2 = 1
        #self.state = letus_db.get_state(action)  # 获取当前状态，假设有一个函数可以获取数据库的当前状态
        reward = -(k1*self.state[0] + k2*self.state[1])  # 奖励函数，线性组合状态
        done = False
        return self.state.copy(), reward, done, {}
    
class LetusDB:
    def __init__(self):
        # 初始化数据库状态
        self.t_b = 256
        self.t_d = 64
        self.time_cost = 0
        self.space_cost = 0

    def reset(self):
        # 重置数据库状态
        self.t_b = 256
        self.t_d = 64
    
    def get_time_cost(self, time_cost):
        # 从C++接收时间开销参数
        self.time_cost = float(time_cost)  # 暂时返回t_b作为时间开销的示例

    def get_space_cost(self, space_cost):
        # 从C++接收空间开销参数
        self.space_cost = float(space_cost)  # 暂时返回t_d作为空间开销的示例

    def get_state(self, t_b, t_d):
        # 根据动作获取当前状态，假设有一个函数可以获取数据库的当前状态
        self.t_b = t_b
        self.t_d = t_d
        return time_cost, space_cost


# 定义 Actor 网络（策略网络）
class Actor(nn.Module):
    def __init__(self, state_dim, action_dim, max_action):
        super(Actor, self).__init__()
        self.layer1 = nn.Linear(state_dim, 256)  # 输入层到隐藏层1，大小为 256
        self.ln1 = nn.LayerNorm(256) # 使用 LayerNorm 进行归一化
        self.layer2 = nn.Linear(256, 256)  # 隐藏层1到隐藏层2，大小为 256
        self.ln2 = nn.LayerNorm(256)
        self.layer3 = nn.Linear(256, action_dim)  # 隐藏层2到输出层，输出动作维度
        self.max_action = max_action  # 动作的最大值，用于限制输出范围
        self.activation = nn.LeakyReLU(0.01) # 使用 LeakyReLU 激活函数，避免死神经元问题

        nn.init.xavier_normal_(self.layer1.weight) # 使用 Xavier 初始化权重
        nn.init.xavier_normal_(self.layer2.weight)
        nn.init.xavier_normal_(self.layer3.weight)
 
    def forward(self, state):
        
        x = self.activation(self.ln1(self.layer1(state)))
        x = self.activation(self.ln2(self.layer2(x)))
        '''
        x = torch.relu(self.layer1(state))  # 使用 ReLU 激活函数处理隐藏层1
        x = self.activation(x)
        x = torch.relu(self.layer2(x))  # 使用 ReLU 激活函数处理隐藏层2
        x = self.activation(x)
        '''
        x = torch.tanh(self.layer3(x)) * self.max_action  # 使用 Tanh 激活函数，并放大到动作范围
        #print(self.max_action)
        return x  # 返回输出动作
 
# 定义 Critic 网络（价值网络）
class Critic(nn.Module):
    def __init__(self, state_dim, action_dim):
        super(Critic, self).__init__()
        self.q1 = nn.Sequential(
            nn.Linear(state_dim + action_dim, 256),
            nn.LayerNorm(256),          # 使用 LayerNorm 替代 BatchNorm
            nn.LeakyReLU(0.01),
            nn.Linear(256, 256),
            nn.LayerNorm(256),
            nn.LeakyReLU(0.01),
            nn.Linear(256, 1)
        )
        # 定义第二个 Q 网络 (Q2)
        self.q2 = nn.Sequential(
            nn.Linear(state_dim + action_dim, 256),
            nn.LayerNorm(256),
            nn.LeakyReLU(0.01),
            nn.Linear(256, 256),
            nn.LayerNorm(256),
            nn.LeakyReLU(0.01),
            nn.Linear(256, 1)
        )
        # 初始化权重
        self._init_weights()

    def _init_weights(self):
        # 对两个 Q 网络分别进行 Xavier 初始化
        for layer in [self.q1, self.q2]:
            nn.init.xavier_normal_(layer[0].weight)
            nn.init.xavier_normal_(layer[3].weight)
            nn.init.xavier_normal_(layer[6].weight)
 
    def forward(self, state, action):
        x = torch.cat([state, action], dim=1)
        q1 = self.q1(x)
        q2 = self.q2(x)
        return q1, q2
    
    def q1_value(self, state, action):
        # 仅返回 Q1 的值（用于 Actor 更新）
        x = torch.cat([state, action], dim=1)
        return self.q1(x)
 
# 定义经验回放池
class ReplayBuffer:
    def __init__(self, max_size):
        self.buffer = deque(maxlen=max_size)  # 初始化一个双端队列，设置最大容量
 
    def add(self, state, action, reward, next_state, done):
        self.buffer.append((state, action, reward, next_state, done))  # 将经验存入队列
 
    def sample(self, batch_size):
        batch = random.sample(self.buffer, batch_size)  # 随机采样一个小批量数据
        states, actions, rewards, next_states, dones = zip(*batch)  # 解压采样数据
        return (np.array(states), np.array(actions), np.array(rewards),
                np.array(next_states), np.array(dones))  # 返回 NumPy 数组格式的数据
 
    def size(self):
        return len(self.buffer)  # 返回经验池中当前存储的样本数量

class SumTree:
    """SumTree 数据结构实现优先级采样"""
    def __init__(self, capacity):
        self.capacity = capacity
        self.tree = np.zeros(2 * capacity - 1)
        self.data = np.zeros(capacity, dtype=object)
        self.write = 0

    def _propagate(self, idx, change):
        parent = (idx - 1) // 2
        self.tree[parent] += change
        if parent != 0:
            self._propagate(parent, change)

    def _retrieve(self, idx, s):
        left = 2 * idx + 1
        if left >= len(self.tree):
            return idx
        if s <= self.tree[left]:
            return self._retrieve(left, s)
        else:
            return self._retrieve(left + 1, s - self.tree[left])

    def total(self):
        return self.tree[0]

    def add(self, priority, data):
        idx = self.write + self.capacity - 1
        self.data[self.write] = data
        self.update(idx, priority)
        self.write = (self.write + 1) % self.capacity

    def update(self, idx, priority):
        change = priority - self.tree[idx]
        self.tree[idx] = priority
        self._propagate(idx, change)

    def get(self, s):
        idx = self._retrieve(0, s)
        data_idx = idx - self.capacity + 1
        return idx, self.tree[idx], self.data[data_idx]
    
    def get_current_size(self):
        """返回当前已存储的样本数量"""
        return min(self.write, self.capacity)

class PrioritizedReplayBuffer:
    """支持优先级采样的经验回放池"""
    def __init__(self, capacity, alpha=0.6, beta=0.4, beta_increment=0.001):
        self.tree = SumTree(capacity)
        self.alpha = alpha  # 控制优先级的指数权重
        self.beta = beta    # 重要性采样校正系数
        self.beta_increment = beta_increment
        self.max_priority = 1.0  # 初始优先级

    def add(self, state, action, reward, next_state, done):
        data = (state, action, reward, next_state, done)
        self.tree.add(self.max_priority, data)  # 初始优先级设为最大值

    def sample(self, batch_size):
        batch = []
        idxs = []
        segment = self.tree.total() / batch_size
        priorities = []

        self.beta = np.min([1.0, self.beta + self.beta_increment])

        for i in range(batch_size):
            a = segment * i
            b = segment * (i + 1)
            s = np.random.uniform(a, b)
            idx, priority, data = self.tree.get(s)
            priorities.append(priority)
            batch.append(data)
            idxs.append(idx)

        # 计算重要性采样权重
        sampling_prob = np.array(priorities) / self.tree.total()
        is_weight = np.power(len(self.tree.data) * sampling_prob, -self.beta)
        is_weight /= is_weight.max()

        # 解压数据
        states, actions, rewards, next_states, dones = zip(*batch)
        return (
            np.array(states),
            np.array(actions),
            np.array(rewards),
            np.array(next_states),
            np.array(dones),
            idxs,
            is_weight,
        )

    def update_priorities(self, idxs, priorities):
        """训练后更新样本的优先级"""
        for idx, priority in zip(idxs, priorities):
            priority = np.power(priority + 1e-6, self.alpha)  # 防止零优先级
            self.tree.update(idx, priority)
            self.max_priority = max(self.max_priority, priority)

    def size(self):
        """返回当前经验池中的样本数量"""
        return self.tree.get_current_size()

# DDPG智能体类定义
class DDPGAgent:
    # 初始化方法，设置智能体的参数和模型
    def __init__(self, state_dim, action_dim, max_action, gamma=0.95, tau=0.01, buffer_size=100000, batch_size=128):
        # 定义actor网络（策略网络）及其目标网络
        self.actor = Actor(state_dim, action_dim, max_action)
        self.actor_target = Actor(state_dim, action_dim, max_action)
        # 将目标actor网络的参数初始化为与actor网络一致
        self.actor_target.load_state_dict(self.actor.state_dict())
        # 定义actor网络的优化器
        self.actor_optimizer = optim.Adam(self.actor.parameters(), lr=1e-4)
 
        # 定义critic网络（值网络）及其目标网络
        self.critic = Critic(state_dim, action_dim)
        self.critic_target = Critic(state_dim, action_dim)
        # 将目标critic网络的参数初始化为与critic网络一致
        self.critic_target.load_state_dict(self.critic.state_dict())
        # 定义critic网络的优化器
        self.critic_optimizer = optim.Adam(self.critic.parameters(), lr=1e-3, weight_decay=1e-4)
 
        # 保存动作的最大值，用于限制动作范围
        self.max_action = max_action
        # 折扣因子，用于奖励的时间折扣
        self.gamma = gamma
        # 软更新系数，用于目标网络的更新
        self.tau = tau
        # 初始化经验回放池
        self.replay_buffer = PrioritizedReplayBuffer(buffer_size)#ReplayBuffer(buffer_size)
        # 每次训练的批量大小
        self.batch_size = batch_size
 
    # 选择动作的方法
    def select_action(self, state):
        # 将状态转换为张量
        state = torch.FloatTensor(state.reshape(1, -1))
        # 使用actor网络预测动作，并将结果转换为NumPy数组
        #print(state.shape)
        action = self.actor(state).detach().cpu().numpy().flatten() #actor网络的forward方法生成动作张量，返回一维数组
        noise = np.random.normal(0, 0.1, size=action.shape)
        action = np.clip(action + noise, -self.max_action, self.max_action)
        return action
 
    # 训练方法
    def train(self):
        # 如果回放池中样本数量不足，直接返回
        if self.replay_buffer.size() < self.batch_size:
            return
 
        # 从回放池中采样一批数据
        states, actions, rewards, next_states, dones, idxs, is_weights = self.replay_buffer.sample(self.batch_size)
 
        # 将采样的数据转换为张量
        states = torch.FloatTensor(states)
        actions = torch.FloatTensor(actions)
        rewards = torch.FloatTensor(rewards).unsqueeze(1)  # 添加一个维度以匹配Q值维度
        next_states = torch.FloatTensor(next_states)
        dones = torch.FloatTensor(dones).unsqueeze(1)  # 添加一个维度以匹配Q值维度
        is_weights = torch.FloatTensor(is_weights).unsqueeze(1)
 
        # 计算critic的损失
        with torch.no_grad():  # 关闭梯度计算
            next_actions = self.actor_target(next_states)  # 使用目标actor网络预测下一步动作
            noise = torch.clamp(torch.randn_like(next_actions) * 0.1, -0.2, 0.2)  # 添加噪声
            next_actions = torch.clamp(next_actions + noise, -self.max_action, self.max_action)
            target_q1, target_q2 = self.critic_target(next_states, next_actions)  # 目标Q值
            # 使用贝尔曼方程更新目标Q值
            target_q = torch.min(target_q1, target_q2)
            target_q = rewards + (1 - dones) * self.gamma * target_q
 
        # 当前Q值
        current_q1, current_q2 = self.critic(states, actions)
        td_errors = torch.abs(target_q - current_q1).detach().cpu().numpy().flatten()
        self.replay_buffer.update_priorities(idxs, td_errors)
        # 均方误差损失
        critic_loss = torch.mean((current_q1 - target_q).pow(2) * is_weights) + torch.mean((current_q2 - target_q).pow(2) * is_weights)
        #critic_loss = nn.MSELoss()(current_q1, target_q) + nn.MSELoss()(current_q2, target_q)
 
        # 优化critic网络
        self.critic_optimizer.zero_grad()
        critic_loss.backward()
        torch.nn.utils.clip_grad_norm_(self.critic.parameters(), max_norm=1.0)  # 梯度裁剪
        self.critic_optimizer.step()
 
        # 计算actor的损失
        actor_loss = -self.critic.q1_value(states, self.actor(states)).mean()  # 策略梯度目标为最大化Q值
 
        # 优化actor网络
        self.actor_optimizer.zero_grad()
        actor_loss.backward()
        self.actor_optimizer.step()
 
        # 更新目标网络参数（软更新）
        for target_param, param in zip(self.critic_target.parameters(), self.critic.parameters()):
            target_param.data.copy_(self.tau * param.data + (1 - self.tau) * target_param.data)
 
        for target_param, param in zip(self.actor_target.parameters(), self.actor.parameters()):
            target_param.data.copy_(self.tau * param.data + (1 - self.tau) * target_param.data)
 
    # 将样本添加到回放池中
    def add_to_replay_buffer(self, state, action, reward, next_state, done):
        self.replay_buffer.add(state, action, reward, next_state, done)
 
# 绘制学习曲线的方法
import matplotlib.pyplot as plt
 
def train_ddpg(env_name, episodes=1000, max_steps=400):
    # 创建环境
    env = gym.make(env_name) #定义一个letus数据库的环境，包括状态空间和动作空间
    state_dim = env.observation_space.shape[0]  # 状态空间维度，数据库性能指标
    action_dim = env.action_space.shape[0]  # 动作空间维度，数据库配置参数tb和td等等
    max_action = float(env.action_space.high[0])  # 动作最大值
    #print(env.observation_space)
 
    # 初始化DDPG智能体
    agent = DDPGAgent(state_dim, action_dim, max_action)
    rewards = []  # 用于存储每个episode的奖励
 
    for episode in range(episodes):
        state, _ = env.reset()  # 重置环境，获取初始状态  note:当环境是letus数据库时，reset()方法需重写，返回值是一个元组，第一个元素是状态，第二个元素是信息字典
        episode_reward = 0  # 初始化每轮奖励为0
        for step in range(max_steps):
            # 选择动作
            action = agent.select_action(state)
            # 执行动作，获取环境反馈
            next_state, reward, done, _, _ = env.step(action) #letus数据库的step方法，根据当前参数返回下一个状态和奖励
            # 将样本存入回放池
            agent.add_to_replay_buffer(state, action, reward, next_state, done)
 
            # 训练智能体
            agent.train()
            # 更新当前状态
            state = next_state
            # 累加奖励
            episode_reward += reward
 
            if done:  # 如果完成（到达终止状态），结束本轮
                break
 
        # 记录每轮的累计奖励
        rewards.append(episode_reward)
        print(f"Episode: {episode + 1}, Step: {step + 1}, Reward: {episode_reward}")
 
    # 绘制学习曲线
    plt.plot(rewards)
    plt.title("Learning Curve")
    plt.xlabel("Episodes")
    plt.ylabel("Cumulative Reward")
    plt.show()
 
    env.close()  # 关闭环境
 
 
# 主函数运行
if __name__ == "__main__":
    if not hasattr(np, 'bool8'):
        np.bool8 = np.bool_
    # print(np.bool8)
    # 定义环境名称和训练轮数
    env_name = "Pendulum-v1" #改为letus数据库
    episodes = int(1000)
    # 开始训练
    train_ddpg(env_name, episodes=episodes)
    