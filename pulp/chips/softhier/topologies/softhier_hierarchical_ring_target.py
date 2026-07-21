from pulp.chips.softhier.softhier_target_base import SoftHierTargetBase
from pulp.chips.softhier.softhier_hierarchical_ring.softhier_system import SoftHierPlatform


class Target(SoftHierTargetBase):
    model = SoftHierPlatform
    name = "hierarchical_ring"
