from pulp.chips.softhier.softhier_target_base import SoftHierTargetBase
from pulp.chips.softhier.softhier_2d_torus.softhier_system import SoftHierPlatform


class Target(SoftHierTargetBase):
    model = SoftHierPlatform
    name = "2d_torus"
