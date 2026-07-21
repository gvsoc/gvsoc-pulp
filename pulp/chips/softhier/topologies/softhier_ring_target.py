from pulp.chips.softhier.softhier_target_base import SoftHierTargetBase
from pulp.chips.softhier.softhier_ring.softhier_system import SoftHierPlatform


class Target(SoftHierTargetBase):
    model = SoftHierPlatform
    name = "ring"
