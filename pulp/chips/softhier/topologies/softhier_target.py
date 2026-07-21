from pulp.chips.softhier.softhier_target_base import SoftHierTargetBase
from pulp.chips.softhier.softhier.softhier_system import SoftHierPlatform


class Target(SoftHierTargetBase):
    model = SoftHierPlatform
    name = "softhier"
