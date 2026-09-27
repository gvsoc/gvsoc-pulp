from pulp.chips.softhier_v2.softhier_target_base import SoftHierTargetBase
from pulp.chips.softhier_v2.softhier_system_base import SoftHierPlatform


class Platform(SoftHierPlatform):
    topology = "hierarchical_ring"


class Target(SoftHierTargetBase):
    model = Platform
    name = "hierarchical_ring"
