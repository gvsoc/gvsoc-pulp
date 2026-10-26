from pulp.chips.mc_pool.mc_pool_target_base import McPoolTargetBase
from pulp.chips.mc_pool.mc_pool_system_base import McPoolPlatform


class Platform(McPoolPlatform):
    config = "tensorpool"


class Target(McPoolTargetBase):
    model = Platform
    name = "mc_pool_tensorpool"
