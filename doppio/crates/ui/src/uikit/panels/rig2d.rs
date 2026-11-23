//! Rig 2D panel: the Live2D-style rig of a sprite (bones, skinned mesh and the parameters that drive them).
//!
//! Like the PhaseShift panel this is a pure view over [`Rig2dModel`]. Parameters are the interface: the sliders here
//! set the same named values a PhaseShift state or a parameter clip would write, so what the artist sees is what the
//! engine skins. [`Rig2dModel::warnings`] reports what skinning would get wrong: vertices with no bone, weights that
//! do not sum to one and bindings that point at missing bones or parameters.

use crate::uikit::components::{button, list_row, panel, slider, status, text_field};
use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::{column, row, text};
use iced::{Element, Length};

#[derive(Debug, Clone, PartialEq)]
pub struct BoneRow {
    pub name: String,
    /// Index of the parent bone, or `None` for a root.
    pub parent: Option<usize>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct RigParameterRow {
    pub name: String,
    pub min: f32,
    pub default: f32,
    pub max: f32,
    pub value: f32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct VertexRow {
    /// Bone index and weight of each influence.
    pub influences: Vec<(usize, f32)>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct BindingRow {
    pub parameter: usize,
    pub bone: usize,
    pub channel: &'static str,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct Rig2dModel {
    pub owner: String,
    pub bones: Vec<BoneRow>,
    pub parameters: Vec<RigParameterRow>,
    pub vertices: Vec<VertexRow>,
    pub triangles: usize,
    pub bindings: Vec<BindingRow>,
}

/// Messages the panel emits. The app writes the parameter to the engine and records an undo step.
#[derive(Debug, Clone, PartialEq)]
pub enum Edit {
    SelectBone(usize),
    SetParameter {
        index: usize,
        value: f32,
    },
    ResetParameters,
    /// The attach path field changed (the draft lives in the app).
    SetDraft(String),
    /// Attaches the draft path to the selected entity.
    Attach,
}

impl Rig2dModel {
    /// Applies an edit to the model. Returns true when the model changed. Bone selection is kept by the app.
    pub fn apply(&mut self, edit: &Edit) -> bool {
        match *edit {
            Edit::SelectBone(_) | Edit::SetDraft(_) | Edit::Attach => false,
            Edit::SetParameter { index, value } => match self.parameters.get_mut(index) {
                Some(p) => {
                    let clamped = value.clamp(p.min, p.max);
                    let changed = p.value != clamped;
                    p.value = clamped;
                    changed
                }
                None => false,
            },
            Edit::ResetParameters => {
                let mut changed = false;
                for p in &mut self.parameters {
                    if p.value != p.default {
                        p.value = p.default;
                        changed = true;
                    }
                }
                changed
            }
        }
    }

    /// Problems that make skinning wrong or silently ignored.
    pub fn warnings(&self) -> Vec<String> {
        let mut out = Vec::new();
        let bone_count = self.bones.len();
        for (i, v) in self.vertices.iter().enumerate() {
            if v.influences.is_empty() {
                out.push(format!(
                    "vertex {i} has no bone and keeps its rest position"
                ));
                continue;
            }
            let total: f32 = v.influences.iter().map(|(_, w)| *w).sum();
            if (total - 1.0).abs() > 1e-3 {
                out.push(format!("vertex {i} weights sum to {total:.3}, not 1"));
            }
            if v.influences.iter().any(|(bone, _)| *bone >= bone_count) {
                out.push(format!("vertex {i} is weighted to a missing bone"));
            }
        }
        for b in &self.bindings {
            if b.parameter >= self.parameters.len() {
                out.push(format!("binding on missing parameter {}", b.parameter));
            }
            if b.bone >= bone_count {
                out.push(format!("binding on missing bone {}", b.bone));
            }
        }
        out
    }

    /// Parameters that differ from their default: the pose is not at rest.
    pub fn posed_count(&self) -> usize {
        self.parameters
            .iter()
            .filter(|p| p.value != p.default)
            .count()
    }
}

// ── Ids ───────────────────────────────────────────────────────────────────

pub fn bone_row(index: usize) -> UiId {
    UiId::dynamic(format!("rig2d.bones.row.{index}"))
}

pub fn parameter_slider(index: usize) -> UiId {
    UiId::dynamic(format!("rig2d.parameters.slider.{index}"))
}

pub fn reset_button() -> UiId {
    UiId::fixed("rig2d.parameters.reset")
}

pub fn attach_path_field() -> UiId {
    UiId::fixed("rig2d.attach.path")
}

pub fn attach_button() -> UiId {
    UiId::fixed("rig2d.attach.button")
}

// ── View ──────────────────────────────────────────────────────────────────

/// The panel. `model` is None for an entity without a rig: the attach row is still there, to give it one.
/// `draft` is the path in the attach field.
pub fn view<'a>(
    model: Option<&'a Rig2dModel>,
    draft: &'a str,
    selected_bone: Option<usize>,
) -> Element<'a, Edit> {
    let Some(model) = model else {
        let body = column![
            attach_row(draft),
            status::note("Attach a .rig2d file to this entity to pose its bones and parameters."),
        ]
        .spacing(6);
        return column![panel::title("Rig 2D", None), panel::body(body.into())].into();
    };
    let subtitle = if model.owner.is_empty() {
        None
    } else {
        Some(model.owner.clone())
    };
    let mut body = column![
        attach_row(draft),
        heading("Parameters"),
        parameters(model),
        button::compact(reset_button(), "Reset pose", Edit::ResetParameters),
        heading("Bones"),
        bones(model, selected_bone),
        heading("Mesh"),
        text(format!(
            "{} vertices · {} triangles · {} bindings · {} posed",
            model.vertices.len(),
            model.triangles,
            model.bindings.len(),
            model.posed_count()
        ))
        .size(11)
        .color(theme::TEXT),
    ]
    .spacing(6);

    let warnings = model.warnings();
    if !warnings.is_empty() {
        body = body.push(heading("Warnings"));
        for w in warnings {
            body = body.push(status::chip(w, theme::ChipTone::Warning));
        }
    }

    column![panel::title("Rig 2D", subtitle), panel::body(body.into())].into()
}

/// The attach path field and its button: Enter or the button attaches the path to the selected entity.
fn attach_row(draft: &str) -> Element<'_, Edit> {
    row![
        text_field::submit(
            attach_path_field(),
            "assets/hero.rig2d",
            draft,
            Edit::SetDraft,
            Edit::Attach,
        ),
        button::compact(attach_button(), "Attach", Edit::Attach),
    ]
    .spacing(4)
    .into()
}

fn heading<'a>(label: &'a str) -> Element<'a, Edit> {
    text(label).size(11).color(theme::TEXT_MUTED).into()
}

fn parameters(model: &Rig2dModel) -> Element<'_, Edit> {
    if model.parameters.is_empty() {
        return text("This rig has no parameters.")
            .size(11)
            .color(theme::TEXT_MUTED)
            .into();
    }
    let mut list = column![].spacing(4);
    for (index, p) in model.parameters.iter().enumerate() {
        list = list.push(
            row![
                text(p.name.as_str())
                    .size(12)
                    .color(theme::TEXT)
                    .width(Length::FillPortion(2)),
                slider::property(
                    parameter_slider(index),
                    "",
                    p.min..=p.max,
                    p.value,
                    move |value| Edit::SetParameter { index, value },
                ),
            ]
            .spacing(8),
        );
    }
    list.into()
}

fn bones(model: &Rig2dModel, selected: Option<usize>) -> Element<'_, Edit> {
    if model.bones.is_empty() {
        return text("No bones. Import a rig to pose it.")
            .size(11)
            .color(theme::TEXT_MUTED)
            .into();
    }
    let mut list = column![].spacing(2);
    for (index, bone) in model.bones.iter().enumerate() {
        let depth = depth_of(model, index);
        let label = format!("{}{}", "  ".repeat(depth), bone.name);
        list = list.push(list_row::view(
            bone_row(index),
            label,
            selected == Some(index),
            Edit::SelectBone(index),
            Edit::SelectBone(index),
            Edit::SelectBone(index),
        ));
    }
    list.into()
}

/// Depth of a bone in the hierarchy. A cycle stops at the bound instead of looping.
fn depth_of(model: &Rig2dModel, index: usize) -> usize {
    let mut depth = 0;
    let mut current = model.bones.get(index).and_then(|b| b.parent);
    while let Some(parent) = current {
        depth += 1;
        if depth > model.bones.len() {
            break;
        }
        current = model.bones.get(parent).and_then(|b| b.parent);
    }
    depth
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Rig2dModel {
        Rig2dModel {
            owner: "Hero".into(),
            bones: vec![
                BoneRow {
                    name: "root".into(),
                    parent: None,
                },
                BoneRow {
                    name: "arm".into(),
                    parent: Some(0),
                },
            ],
            parameters: vec![RigParameterRow {
                name: "Bend".into(),
                min: -1.0,
                default: 0.0,
                max: 1.0,
                value: 0.0,
            }],
            vertices: vec![
                VertexRow {
                    influences: vec![(0, 1.0)],
                },
                VertexRow {
                    influences: vec![(0, 0.5), (1, 0.5)],
                },
            ],
            triangles: 1,
            bindings: vec![BindingRow {
                parameter: 0,
                bone: 1,
                channel: "rotation",
            }],
        }
    }

    #[test]
    fn set_parameter_clamps_and_reports_changes() {
        let mut m = sample();
        assert!(m.apply(&Edit::SetParameter {
            index: 0,
            value: 3.0
        }));
        assert_eq!(m.parameters[0].value, 1.0);
        assert!(!m.apply(&Edit::SetParameter {
            index: 0,
            value: 3.0
        }));
        assert!(
            !m.apply(&Edit::SetParameter {
                index: 9,
                value: 0.5
            }),
            "unknown parameter is ignored"
        );
    }

    #[test]
    fn reset_returns_every_parameter_to_its_default() {
        let mut m = sample();
        m.apply(&Edit::SetParameter {
            index: 0,
            value: 0.7,
        });
        assert_eq!(m.posed_count(), 1);
        assert!(m.apply(&Edit::ResetParameters));
        assert_eq!(m.parameters[0].value, 0.0);
        assert_eq!(m.posed_count(), 0);
        assert!(!m.apply(&Edit::ResetParameters));
    }

    #[test]
    fn selecting_a_bone_is_not_a_model_change() {
        let mut m = sample();
        assert!(!m.apply(&Edit::SelectBone(1)));
    }

    #[test]
    fn a_consistent_rig_has_no_warnings() {
        assert!(sample().warnings().is_empty());
    }

    #[test]
    fn warnings_cover_unweighted_vertices_bad_sums_and_missing_references() {
        let mut m = sample();
        m.vertices.push(VertexRow { influences: vec![] });
        m.vertices.push(VertexRow {
            influences: vec![(0, 0.4)],
        });
        m.vertices.push(VertexRow {
            influences: vec![(7, 1.0)],
        });
        m.bindings.push(BindingRow {
            parameter: 4,
            bone: 9,
            channel: "scale",
        });
        let w = m.warnings();
        assert!(w.iter().any(|s| s.contains("vertex 2 has no bone")));
        assert!(w
            .iter()
            .any(|s| s.contains("vertex 3 weights sum to 0.400")));
        assert!(w
            .iter()
            .any(|s| s.contains("vertex 4 is weighted to a missing bone")));
        assert!(w.iter().any(|s| s.contains("missing parameter 4")));
        assert!(w.iter().any(|s| s.contains("missing bone 9")));
    }

    #[test]
    fn bone_depth_follows_the_parent_chain_and_survives_cycles() {
        let m = sample();
        assert_eq!(depth_of(&m, 0), 0);
        assert_eq!(depth_of(&m, 1), 1);

        let mut cyclic = sample();
        cyclic.bones[0].parent = Some(1);
        assert!(depth_of(&cyclic, 0) <= cyclic.bones.len() + 1);
    }

    #[test]
    fn view_builds_for_a_rig_and_an_empty_model() {
        let _ = view(Some(&sample()), "", Some(1));
        let _ = view(None, "assets/hero.rig2d", None);
    }

    #[test]
    fn ids_are_stable_and_hierarchical() {
        assert_eq!(bone_row(2).key(), "rig2d.bones.row.2");
        assert_eq!(parameter_slider(0).key(), "rig2d.parameters.slider.0");
        assert_eq!(reset_button().key(), "rig2d.parameters.reset");
    }
}
