//! 测试专用的房间级 GA 回包闸门。存储操作照常执行，只控制结果何时可见。
use super::global_archive::GaOutcome;
use std::collections::HashMap;

#[derive(Default)]
pub struct GaTestControl {
    pub gate: Option<(String, usize)>,
    pub held: Vec<(String, i32, GaOutcome)>,
    pub calls: HashMap<String, usize>,
}

impl std::fmt::Debug for GaTestControl {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("GaTestControl")
            .field("gate", &self.gate)
            .field("held", &self.held.len())
            .field("calls", &self.calls)
            .finish()
    }
}

impl GaTestControl {
    pub fn intercept(&mut self, op: &str, id: i32, outcome: GaOutcome) -> Option<GaOutcome> {
        *self.calls.entry(op.to_owned()).or_default() += 1;
        if let Some((target, count)) = self.gate.as_mut() {
            if target == op && *count > 0 {
                *count -= 1;
                self.held.push((op.to_owned(), id, outcome));
                if *count == 0 {
                    self.gate = None;
                }
                return None;
            }
        }
        Some(outcome)
    }
}
