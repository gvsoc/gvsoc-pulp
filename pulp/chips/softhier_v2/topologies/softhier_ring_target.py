from pulp.chips.softhier_v2.softhier_target_base import SoftHierTargetBase
from pulp.chips.softhier_v2.softhier_system_base import SoftHierPlatform


class Platform(SoftHierPlatform):
    topology = "ring"


class Target(SoftHierTargetBase):
    model = Platform
    name = "ring"
