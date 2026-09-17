from pulp.chips.softhier.softhier_target_base import SoftHierTargetBase
from pulp.chips.softhier.softhier_system_base import SoftHierPlatform


class Platform(SoftHierPlatform):
    topology = "2d_mesh"


class Target(SoftHierTargetBase):
    model = Platform
    name = "2d_mesh"
