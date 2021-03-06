#include <float.h>

#include "headers/advanced-scene-switcher.hpp"
#include "headers/volume-control.hpp"
#include "headers/utility.hpp"

bool AudioSwitch::pause = false;
static QMetaObject::Connection addPulse;

void AdvSceneSwitcher::on_audioAdd_clicked()
{
	std::lock_guard<std::mutex> lock(switcher->m);
	switcher->audioSwitches.emplace_back();

	AudioSwitchWidget *sw =
		new AudioSwitchWidget(this, &switcher->audioSwitches.back());

	listAddClicked(ui->audioSwitches, sw, ui->audioAdd, &addPulse);

	ui->audioHelp->setVisible(false);
}

void AdvSceneSwitcher::on_audioRemove_clicked()
{
	QListWidgetItem *item = ui->audioSwitches->currentItem();
	if (!item) {
		return;
	}

	{
		std::lock_guard<std::mutex> lock(switcher->m);
		int idx = ui->audioSwitches->currentRow();
		auto &switches = switcher->audioSwitches;
		switches.erase(switches.begin() + idx);
	}

	delete item;
}

void AdvSceneSwitcher::on_audioUp_clicked()
{
	int index = ui->audioSwitches->currentRow();
	if (!listMoveUp(ui->audioSwitches)) {
		return;
	}

	AudioSwitchWidget *s1 =
		(AudioSwitchWidget *)ui->audioSwitches->itemWidget(
			ui->audioSwitches->item(index));
	AudioSwitchWidget *s2 =
		(AudioSwitchWidget *)ui->audioSwitches->itemWidget(
			ui->audioSwitches->item(index - 1));
	AudioSwitchWidget::swapSwitchData(s1, s2);

	std::lock_guard<std::mutex> lock(switcher->m);

	std::swap(switcher->audioSwitches[index],
		  switcher->audioSwitches[index - 1]);
}

void AdvSceneSwitcher::on_audioDown_clicked()
{
	int index = ui->audioSwitches->currentRow();

	if (!listMoveDown(ui->audioSwitches)) {
		return;
	}

	AudioSwitchWidget *s1 =
		(AudioSwitchWidget *)ui->audioSwitches->itemWidget(
			ui->audioSwitches->item(index));
	AudioSwitchWidget *s2 =
		(AudioSwitchWidget *)ui->audioSwitches->itemWidget(
			ui->audioSwitches->item(index + 1));
	AudioSwitchWidget::swapSwitchData(s1, s2);

	std::lock_guard<std::mutex> lock(switcher->m);

	std::swap(switcher->audioSwitches[index],
		  switcher->audioSwitches[index + 1]);
}

void AdvSceneSwitcher::on_audioFallback_toggled(bool on)
{
	if (loading || !switcher) {
		return;
	}

	std::lock_guard<std::mutex> lock(switcher->m);
	switcher->audioFallback.enable = on;
}

void SwitcherData::checkAudioSwitchFallback(OBSWeakSource &scene,
					    OBSWeakSource &transition)
{
	bool durationReached =
		((unsigned long long)audioFallback.matchCount * interval) /
			1000.0 >=
		audioFallback.duration;

	if (durationReached) {

		scene = audioFallback.getScene();
		transition = audioFallback.transition;

		if (verbose) {
			audioFallback.logMatch();
		}
	}

	audioFallback.matchCount++;
}

void SwitcherData::checkAudioSwitch(bool &match, OBSWeakSource &scene,
				    OBSWeakSource &transition)
{
	if (AudioSwitch::pause) {
		return;
	}

	bool fallbackChecked = false; // false if only one or no match

	for (AudioSwitch &s : audioSwitches) {
		if (!s.initialized()) {
			continue;
		}

		obs_source_t *as = obs_weak_source_get_source(s.audioSource);
		bool audioActive = obs_source_active(as);
		obs_source_release(as);

		// peak will have a value from -60 db to 0 db
		bool volumeThresholdreached = false;

		if (s.condition == ABOVE) {
			volumeThresholdreached = ((double)s.peak + 60) * 1.7 >
						 s.volumeThreshold;
		} else {
			volumeThresholdreached = ((double)s.peak + 60) * 1.7 <
						 s.volumeThreshold;
		}

		// Reset for next check
		s.peak = -FLT_MAX;

		if (volumeThresholdreached) {
			s.matchCount++;
		} else {
			s.matchCount = 0;
		}

		bool durationReached =
			((unsigned long long)s.matchCount * interval) /
				1000.0 >=
			s.duration;

		if (volumeThresholdreached && durationReached && audioActive) {
			if (match) {
				checkAudioSwitchFallback(scene, transition);
				fallbackChecked = true;
				break;
			}

			scene = s.getScene();
			transition = s.transition;
			match = true;

			if (verbose) {
				s.logMatch();
			}

			if (!audioFallback.enable) {
				break;
			}
		}
	}

	if (!fallbackChecked) {
		audioFallback.matchCount = 0;
	}
}

void SwitcherData::saveAudioSwitches(obs_data_t *obj)
{
	obs_data_array_t *audioArray = obs_data_array_create();
	for (AudioSwitch &s : switcher->audioSwitches) {
		obs_data_t *array_obj = obs_data_create();

		s.save(array_obj);
		obs_data_array_push_back(audioArray, array_obj);

		obs_data_release(array_obj);
	}
	obs_data_set_array(obj, "audioSwitches", audioArray);
	obs_data_array_release(audioArray);

	audioFallback.save(obj);
}

void SwitcherData::loadAudioSwitches(obs_data_t *obj)
{
	switcher->audioSwitches.clear();

	obs_data_array_t *audioArray = obs_data_get_array(obj, "audioSwitches");
	size_t count = obs_data_array_count(audioArray);

	for (size_t i = 0; i < count; i++) {
		obs_data_t *array_obj = obs_data_array_item(audioArray, i);

		switcher->audioSwitches.emplace_back();
		audioSwitches.back().load(array_obj);

		obs_data_release(array_obj);
	}
	obs_data_array_release(audioArray);

	audioFallback.load(obj);
}

void AdvSceneSwitcher::setupAudioTab()
{
	for (auto &s : switcher->audioSwitches) {
		QListWidgetItem *item;
		item = new QListWidgetItem(ui->audioSwitches);
		ui->audioSwitches->addItem(item);
		AudioSwitchWidget *sw = new AudioSwitchWidget(this, &s);
		item->setSizeHint(sw->minimumSizeHint());
		ui->audioSwitches->setItemWidget(item, sw);
	}

	if (switcher->audioSwitches.size() == 0) {
		addPulse = PulseWidget(ui->audioAdd, QColor(Qt::green));
		ui->audioHelp->setVisible(true);
	} else {
		ui->audioHelp->setVisible(false);
	}

	AudioSwitchFallbackWidget *fb =
		new AudioSwitchFallbackWidget(this, &switcher->audioFallback);
	ui->audioFallbackLayout->addWidget(fb);
	ui->audioFallback->setChecked(switcher->audioFallback.enable);
}

void AudioSwitch::setVolumeLevel(void *data,
				 const float magnitude[MAX_AUDIO_CHANNELS],
				 const float peak[MAX_AUDIO_CHANNELS],
				 const float inputPeak[MAX_AUDIO_CHANNELS])
{
	UNUSED_PARAMETER(magnitude);
	UNUSED_PARAMETER(inputPeak);
	AudioSwitch *s = static_cast<AudioSwitch *>(data);

	for (int i = 1; i < MAX_AUDIO_CHANNELS; i++) {
		if (peak[i] > s->peak) {
			s->peak = peak[i];
		}
	}
}

obs_volmeter_t *AddVolmeterToSource(AudioSwitch *entry, obs_weak_source *source)
{
	obs_volmeter_t *volmeter = obs_volmeter_create(OBS_FADER_LOG);
	obs_volmeter_add_callback(volmeter, AudioSwitch::setVolumeLevel, entry);
	obs_source_t *as = obs_weak_source_get_source(source);
	if (!obs_volmeter_attach_source(volmeter, as)) {
		const char *name = obs_source_get_name(as);
		blog(LOG_WARNING, "failed to attach volmeter to source %s",
		     name);
	}
	obs_source_release(as);

	return volmeter;
}

void AudioSwitch::resetVolmeter()
{
	obs_volmeter_remove_callback(volmeter, setVolumeLevel, this);
	obs_volmeter_destroy(volmeter);

	volmeter = AddVolmeterToSource(this, audioSource);
}

bool AudioSwitch::initialized()
{
	return SceneSwitcherEntry::initialized() && audioSource;
}

bool AudioSwitch::valid()
{
	return !initialized() ||
	       (SceneSwitcherEntry::valid() && WeakSourceValid(audioSource));
}

void AudioSwitch::save(obs_data_t *obj)
{
	SceneSwitcherEntry::save(obj);

	obs_data_set_string(obj, "audioSource",
			    GetWeakSourceName(audioSource).c_str());

	obs_data_set_int(obj, "volume", volumeThreshold);
	obs_data_set_int(obj, "condition", condition);
	obs_data_set_double(obj, "duration", duration);
}

// To be removed in future version
bool loadOldAudio(obs_data_t *obj, AudioSwitch *s)
{
	if (!s) {
		return false;
	}

	const char *scene = obs_data_get_string(obj, "scene");

	if (strcmp(scene, "") == 0) {
		return false;
	}

	s->scene = GetWeakSourceByName(scene);

	const char *transition = obs_data_get_string(obj, "transition");
	s->transition = GetWeakTransitionByName(transition);

	const char *audioSource = obs_data_get_string(obj, "audioSource");
	s->audioSource = GetWeakSourceByName(audioSource);

	s->volumeThreshold = obs_data_get_int(obj, "volume");
	s->condition = (audioCondition)obs_data_get_int(obj, "condition");
	s->duration = obs_data_get_double(obj, "duration");
	s->usePreviousScene = strcmp(scene, previous_scene_name) == 0;

	return true;
}

void AudioSwitch::load(obs_data_t *obj)
{
	if (loadOldAudio(obj, this)) {
		return;
	}

	SceneSwitcherEntry::load(obj);

	const char *audioSourceName = obs_data_get_string(obj, "audioSource");
	audioSource = GetWeakSourceByName(audioSourceName);

	volumeThreshold = obs_data_get_int(obj, "volume");
	condition = (audioCondition)obs_data_get_int(obj, "condition");
	duration = obs_data_get_double(obj, "duration");

	volmeter = AddVolmeterToSource(this, audioSource);
}

void AudioSwitchFallback::save(obs_data_t *obj)
{
	SceneSwitcherEntry::save(obj, "audioFallbackTargetType",
				 "audioFallbackScene",
				 "audioFallbackTransition");

	obs_data_set_bool(obj, "audioFallbackEnable", enable);
	obs_data_set_double(obj, "audioFallbackDuration", duration);
}

void AudioSwitchFallback::load(obs_data_t *obj)
{
	SceneSwitcherEntry::load(obj, "audioFallbackTargetType",
				 "audioFallbackScene",
				 "audioFallbackTransition");

	enable = obs_data_get_bool(obj, "audioFallbackEnable");
	duration = obs_data_get_double(obj, "audioFallbackDuration");
}

AudioSwitch::AudioSwitch(const AudioSwitch &other)
	: SceneSwitcherEntry(other.targetType, other.group, other.scene,
			     other.transition, other.usePreviousScene),
	  audioSource(other.audioSource),
	  volumeThreshold(other.volumeThreshold),
	  condition(other.condition),
	  duration(other.duration)
{
	volmeter = AddVolmeterToSource(this, other.audioSource);
}

AudioSwitch::AudioSwitch(AudioSwitch &&other)
	: SceneSwitcherEntry(other.targetType, other.group, other.scene,
			     other.transition, other.usePreviousScene),
	  audioSource(other.audioSource),
	  volumeThreshold(other.volumeThreshold),
	  condition(other.condition),
	  duration(other.duration),
	  volmeter(other.volmeter)
{
	other.volmeter = nullptr;
}

AudioSwitch::~AudioSwitch()
{
	obs_volmeter_remove_callback(volmeter, setVolumeLevel, this);
	obs_volmeter_destroy(volmeter);
}

AudioSwitch &AudioSwitch::operator=(const AudioSwitch &other)
{
	AudioSwitch t(other);
	swap(*this, t);
	return *this = AudioSwitch(other);
}

AudioSwitch &AudioSwitch::operator=(AudioSwitch &&other) noexcept
{
	if (this == &other) {
		return *this;
	}

	swap(*this, other);

	obs_volmeter_remove_callback(other.volmeter, setVolumeLevel, this);
	obs_volmeter_destroy(other.volmeter);
	other.volmeter = nullptr;

	return *this;
}

void swap(AudioSwitch &first, AudioSwitch &second)
{
	std::swap(first.targetType, second.targetType);
	std::swap(first.group, second.group);
	std::swap(first.scene, second.scene);
	std::swap(first.transition, second.transition);
	std::swap(first.usePreviousScene, second.usePreviousScene);
	std::swap(first.audioSource, second.audioSource);
	std::swap(first.volumeThreshold, second.volumeThreshold);
	std::swap(first.condition, second.condition);
	std::swap(first.duration, second.duration);
	std::swap(first.peak, second.peak);
	std::swap(first.volmeter, second.volmeter);
	first.resetVolmeter();
	second.resetVolmeter();
}

void populateConditionSelection(QComboBox *list)
{
	list->addItem(
		obs_module_text("AdvSceneSwitcher.audioTab.condition.above"));
	list->addItem(
		obs_module_text("AdvSceneSwitcher.audioTab.condition.below"));
}

AudioSwitchWidget::AudioSwitchWidget(QWidget *parent, AudioSwitch *s)
	: SwitchWidget(parent, s, true, true)
{
	audioSources = new QComboBox();
	condition = new QComboBox();
	audioVolumeThreshold = new QSpinBox();
	duration = new QDoubleSpinBox();

	obs_source_t *source = nullptr;
	if (s) {
		source = obs_weak_source_get_source(s->audioSource);
	}
	volMeter = new VolControl(source);
	obs_source_release(source);

	audioVolumeThreshold->setSuffix("%");
	audioVolumeThreshold->setMaximum(100);
	audioVolumeThreshold->setMinimum(0);

	duration->setMinimum(0.0);
	duration->setMaximum(99.000000);
	duration->setSuffix("s");

	QWidget::connect(volMeter->GetSlider(), SIGNAL(valueChanged(int)),
			 audioVolumeThreshold, SLOT(setValue(int)));
	QWidget::connect(audioVolumeThreshold, SIGNAL(valueChanged(int)),
			 volMeter->GetSlider(), SLOT(setValue(int)));
	QWidget::connect(audioVolumeThreshold, SIGNAL(valueChanged(int)), this,
			 SLOT(VolumeThresholdChanged(int)));
	QWidget::connect(condition, SIGNAL(currentIndexChanged(int)), this,
			 SLOT(ConditionChanged(int)));
	QWidget::connect(duration, SIGNAL(valueChanged(double)), this,
			 SLOT(DurationChanged(double)));
	QWidget::connect(audioSources,
			 SIGNAL(currentTextChanged(const QString &)), this,
			 SLOT(SourceChanged(const QString &)));

	AdvSceneSwitcher::populateAudioSelection(audioSources);
	populateConditionSelection(condition);

	if (s) {
		audioSources->setCurrentText(
			GetWeakSourceName(s->audioSource).c_str());
		audioVolumeThreshold->setValue(s->volumeThreshold);
		condition->setCurrentIndex(s->condition);
		duration->setValue(s->duration);
	}

	QHBoxLayout *switchLayout = new QHBoxLayout;
	std::unordered_map<std::string, QWidget *> widgetPlaceholders = {
		{"{{audioSources}}", audioSources},
		{"{{volumeWidget}}", audioVolumeThreshold},
		{"{{condition}}", condition},
		{"{{duration}}", duration},
		{"{{scenes}}", scenes},
		{"{{transitions}}", transitions}};
	placeWidgets(obs_module_text("AdvSceneSwitcher.audioTab.entry"),
		     switchLayout, widgetPlaceholders);

	QVBoxLayout *mainLayout = new QVBoxLayout;

	mainLayout->addLayout(switchLayout);
	mainLayout->addWidget(volMeter);

	setLayout(mainLayout);

	switchData = s;

	loading = false;
}

AudioSwitch *AudioSwitchWidget::getSwitchData()
{
	return switchData;
}

void AudioSwitchWidget::setSwitchData(AudioSwitch *s)
{
	switchData = s;
}

void AudioSwitchWidget::swapSwitchData(AudioSwitchWidget *s1,
				       AudioSwitchWidget *s2)
{
	SwitchWidget::swapSwitchData(s1, s2);

	AudioSwitch *t = s1->getSwitchData();
	s1->setSwitchData(s2->getSwitchData());
	s2->setSwitchData(t);
}

void AudioSwitchWidget::UpdateVolmeterSource()
{
	delete volMeter;
	obs_source_t *soruce =
		obs_weak_source_get_source(switchData->audioSource);
	volMeter = new VolControl(soruce);
	obs_source_release(soruce);

	QLayout *layout = this->layout();
	layout->addWidget(volMeter);

	QWidget::connect(volMeter->GetSlider(), SIGNAL(valueChanged(int)),
			 audioVolumeThreshold, SLOT(setValue(int)));
	QWidget::connect(audioVolumeThreshold, SIGNAL(valueChanged(int)),
			 volMeter->GetSlider(), SLOT(setValue(int)));

	// Slider will default to 0 so set it manually once
	volMeter->GetSlider()->setValue(switchData->volumeThreshold);
}

void AudioSwitchWidget::SourceChanged(const QString &text)
{
	if (loading || !switchData) {
		return;
	}

	std::lock_guard<std::mutex> lock(switcher->m);
	switchData->audioSource = GetWeakSourceByQString(text);
	switchData->resetVolmeter();
	UpdateVolmeterSource();
}

void AudioSwitchWidget::VolumeThresholdChanged(int vol)
{
	if (loading || !switchData) {
		return;
	}

	std::lock_guard<std::mutex> lock(switcher->m);
	switchData->volumeThreshold = vol;
}

void AudioSwitchWidget::ConditionChanged(int cond)
{
	if (loading || !switchData) {
		return;
	}

	std::lock_guard<std::mutex> lock(switcher->m);
	switchData->condition = (audioCondition)cond;
}

void AudioSwitchWidget::DurationChanged(double dur)
{
	if (loading || !switchData) {
		return;
	}

	std::lock_guard<std::mutex> lock(switcher->m);
	switchData->duration = dur;
}

AudioSwitchFallbackWidget::AudioSwitchFallbackWidget(QWidget *parent,
						     AudioSwitchFallback *s)
	: SwitchWidget(parent, s, true, true)
{
	duration = new QDoubleSpinBox();

	duration->setMinimum(0.0);
	duration->setMaximum(99.000000);
	duration->setSuffix("s");

	QWidget::connect(duration, SIGNAL(valueChanged(double)), this,
			 SLOT(DurationChanged(double)));

	if (s) {
		duration->setValue(s->duration);
	}

	QHBoxLayout *mainLayout = new QHBoxLayout;
	std::unordered_map<std::string, QWidget *> widgetPlaceholders = {
		{"{{scenes}}", scenes},
		{"{{duration}}", duration},
		{"{{transitions}}", transitions}};
	placeWidgets(
		obs_module_text("AdvSceneSwitcher.audioTab.multiMatchfallback"),
		mainLayout, widgetPlaceholders);
	setLayout(mainLayout);

	switchData = s;

	loading = false;
}

void AudioSwitchFallbackWidget::DurationChanged(double dur)
{
	if (loading || !switchData) {
		return;
	}

	std::lock_guard<std::mutex> lock(switcher->m);
	switchData->duration = dur;
}
