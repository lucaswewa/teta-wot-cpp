//! The twin Things, written against teta-wot v0.1.0, and their registry.
//!
//! The C++ twin server registers C++ ports of the same Things under the same
//! names, so one configuration file serves either implementation. Twins are
//! added phase by phase; Phase 0 has the counter.

use std::time::Duration;

use teta_wot::prelude::*;
use teta_wot::server::ThingRegistry;

/// A test thing with a counter property and a couple of actions.
#[derive(Thing)]
pub struct TestThing {
    /// A pointless counter.
    #[property(readonly, default = 0)]
    counter: Prop<i64>,
}

#[thing_impl]
impl TestThing {
    /// Increment the counter property.
    ///
    /// This action doesn't do very much - all it does, in fact,
    /// is increment the counter (which may be read using the
    /// `counter` property).
    #[action]
    async fn increment_counter(&self) -> Result<(), ActionError> {
        self.counter.update(|c| *c += 1)?;
        Ok(())
    }

    /// Increment the counter slowly over a minute.
    #[action]
    async fn slowly_increase_counter(&self, ctx: ActionCtx) -> Result<(), ActionError> {
        for _ in 0..60 {
            ctx.sleep(Duration::from_secs(1)).await?;
            self.counter.update(|c| *c += 1)?;
        }
        Ok(())
    }
}

/// The twins, by the names configuration files use.
pub fn registry() -> ThingRegistry {
    ThingRegistry::new().register_as::<TestThing>(&["twins.counter:TestThing", "twins:TestThing"])
}
