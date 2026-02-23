#pragma once

class FixedTimestep
{
	public:
		explicit FixedTimestep
		(
			float timestep = 1.0f / 60.0f,
			float maxFrameTime = 0.25f
		) : 
		m_timestep(timestep),
		m_maxFrameTime(maxFrameTime),
		m_accumulator(0.0f),
		m_frameTime(0.0f),
		m_lastTime(0.0f)
		{}

		// call once per frame, at the top of the game loop
		// pass in the current time (e.g. from glfwGetTime()).
		void accumulate(float currentTime)
		{
			m_frameTime = currentTime - m_lastTime;
			m_lastTime = currentTime;

			// clamp to prevent spiral of death
			if (m_frameTime > m_maxFrameTime)
			{
				m_frameTime = m_maxFrameTime;
			}

			m_accumulator += m_frameTime;
		}

		// call in a while loop. returns true if there is enough accumulated time
		// for one fixed step, and sonsumes that step's worth of time.
		
		bool step()
		{
			if (m_accumulator >= m_timestep)
			{
				m_accumulator -= m_timestep;
				return true;	
			}
			return false;
		}

		// returns the interpolation factor (0.0 to 1.0) representing how far
		// between the previous and current phsyics states we are.
		// use this to interpolate positions for smooth rendering.
		float getAlpha() const
		{
			return m_accumulator / m_timestep;
		}

		// the frame time for the most recent accumulate() call.
		// use this for fram-rate-dependent things like camera input.
		float getFrameTime() const { return m_frameTime; }

		// getters for the configuration values.
		float getTimestep() const { return m_timestep; }

	private:
		float m_timestep;
		float m_maxFrameTime;
		float m_accumulator;
		float m_frameTime;
		float m_lastTime;
};
