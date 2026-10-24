# Web of Things concepts

teta-wot-cpp, like teta-wot, uses the architecture and vocabulary of the [W3C Web of Things](https://www.w3.org/WoT/) rather than inventing its own. The [WoT Architecture](https://www.w3.org/TR/wot-architecture11/) describes it fully. This page outlines the parts that matter here.

## Thing

A Thing represents a piece of hardware or software: a whole instrument (a microscope), a component of one (a stage, a camera), or software (code that tiles a large scan). The W3C defines it as "an abstraction of a physical or a virtual entity whose metadata and interfaces are described by a WoT Thing Description".

In teta-wot-cpp, a Thing is a C++ class with a static `define`, served under a name. Each of its functions is a property, an action or an event: its *interaction affordances*.

## Properties

A property is a state of the Thing that can be read, perhaps written, and observed: a setting, or a status such as a temperature that takes no time to measure. Reading a property should be quick, because clients read them often, and writing one shouldn't start long operations. See [Properties](properties.md).

## Actions

An action makes the Thing do something: start an acquisition, move a stage, change a setting that takes a while. Actions can do more than a property write: set several properties together (as an auto-exposure does), or change the Thing's state over time.

An action is *invoked*, and each run is an *invocation*. teta-wot-cpp runs invocations in the background, so that properties and other actions stay available meanwhile. See [Actions](actions.md).

## Events

An event pushes a notification from the Thing to its consumers when something happens: a temperature too high, an interlock tripped, an image captured. It communicates a transition rather than a state. See [Events](events.md).

## Thing Description

Each Thing is described by a Thing Description (TD): a JSON document of all the ways to interact with it, which the W3C [Thing Description 1.1](https://www.w3.org/TR/wot-thing-description11/) specification defines, with a JSON Schema to validate it against.

- **Affordances.** Each property, action and event, with its data schema (types, ranges, units), title and description.
- **Forms.** How to use each affordance: the URL, the HTTP method and the operation (`readproperty`, `invokeaction`, `observeproperty`, …).
- **Metadata.** The Thing's `id`, security, links, and the profiles it follows.

TDs are higher-level than an OpenAPI document: they describe capabilities, not endpoints, and one affordance can have several forms. So client code written against a TD can be more meaningful, and work with any server that describes itself the same way. OpenAPI is far more widely supported, so teta-wot-cpp serves both ([Documentation](documentation.md)).

## Profiles and bindings

The *HTTP binding* says how affordances map to HTTP requests. The *WoT Profile* narrows that choice to one standard behaviour, so that a consumer written for the profile works with any Thing that follows it. teta-wot-cpp uses teta-wot's `tetathing` profile by default, and the WoT Profile in its `wot` [wire profile](wire_profiles.md).

## Discovery

WoT Discovery says how consumers find Things: a well-known URL, directories of TDs, and DNS-SD on the local network. See [Discovery](discovery.md).
